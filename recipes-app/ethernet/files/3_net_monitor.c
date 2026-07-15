#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define WATCHDOG_DEV "/dev/net_watchdog"
#define SYSFS_PATH_MAX 256
#define BUF_SIZE 1500

static unsigned short checksum(void *data, int len)
{
	unsigned int sum = 0;
	unsigned short *ptr = data;

	while (len > 1) {
		sum += *ptr++;
		len -= 2;
	}

	if (len == 1)
		sum += *(unsigned char *)ptr;

	sum = (sum >> 16) + (sum & 0xffff);
	sum += sum >> 16;

	return (unsigned short)(~sum);
}

static int read_int_sysfs(const char *ifname, const char *entry, int *value)
{
	char path[SYSFS_PATH_MAX];
	char buf[32];
	FILE *fp;

	snprintf(path, sizeof(path), "/sys/class/net/%s/%s", ifname, entry);

	fp = fopen(path, "r");
	if (!fp)
		return -errno;

	if (!fgets(buf, sizeof(buf), fp)) {
		fclose(fp);
		return -EIO;
	}

	fclose(fp);
	*value = atoi(buf);
	return 0;
}

static bool has_ipv4_address(const char *ifname)
{
	struct ifaddrs *ifaddr;
	struct ifaddrs *ifa;
	bool found = false;

	if (getifaddrs(&ifaddr) < 0)
		return false;

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;
		if (strcmp(ifa->ifa_name, ifname) != 0)
			continue;
		if (ifa->ifa_addr->sa_family == AF_INET) {
			found = true;
			break;
		}
	}

	freeifaddrs(ifaddr);
	return found;
}

static bool has_default_route(const char *ifname)
{
	FILE *fp;
	char line[256];
	bool found = false;

	fp = fopen("/proc/net/route", "r");
	if (!fp)
		return false;

	while (fgets(line, sizeof(line), fp)) {
		char iface[IF_NAMESIZE];
		unsigned long destination;

		if (sscanf(line, "%15s %lx", iface, &destination) != 2)
			continue;

		if (strcmp(iface, ifname) == 0 && destination == 0) {
			found = true;
			break;
		}
	}

	fclose(fp);
	return found;
}

static int send_ping_once(const char *target_ip, int timeout_ms, int seq)
{
	int fd;
	struct sockaddr_in dst;
	struct icmphdr icmp;
	char recvbuf[BUF_SIZE];
	struct pollfd pfd;
	ssize_t n;
	int ret;

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd < 0)
		return -errno;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;

	if (inet_pton(AF_INET, target_ip, &dst.sin_addr) != 1) {
		close(fd);
		return -EINVAL;
	}

	memset(&icmp, 0, sizeof(icmp));
	icmp.type = ICMP_ECHO;
	icmp.code = 0;
	icmp.un.echo.id = htons((unsigned short)getpid());
	icmp.un.echo.sequence = htons((unsigned short)seq);
	icmp.checksum = checksum(&icmp, sizeof(icmp));

	n = sendto(fd, &icmp, sizeof(icmp), 0,
		   (struct sockaddr *)&dst, sizeof(dst));
	if (n < 0) {
		ret = -errno;
		close(fd);
		return ret;
	}

	pfd.fd = fd;
	pfd.events = POLLIN;

	ret = poll(&pfd, 1, timeout_ms);
	if (ret <= 0) {
		close(fd);
		return ret == 0 ? -ETIMEDOUT : -errno;
	}

	n = recv(fd, recvbuf, sizeof(recvbuf), 0);
	close(fd);

	if (n < (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr)))
		return -EINVAL;

	struct iphdr *iph = (struct iphdr *)recvbuf;
	struct icmphdr *reply =
		(struct icmphdr *)(recvbuf + (iph->ihl * 4));

	if (reply->type == ICMP_ECHOREPLY &&
	    reply->un.echo.id == htons((unsigned short)getpid()) &&
	    reply->un.echo.sequence == htons((unsigned short)seq))
		return 0;

	return -EAGAIN;
}

static int request_restart(unsigned int ifindex)
{
	FILE *fp;

	fp = fopen(WATCHDOG_DEV, "w");
	if (!fp)
		return -errno;

	fprintf(fp, "RESTART %u\n", ifindex);
	fclose(fp);

	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <ifname> <target-ip> [fail-threshold] [timeout-ms] [interval-sec]\n",
		prog);
	fprintf(stderr, "Example: %s eth1 192.168.79.1 3 1000 5\n", prog);
}

int main(int argc, char **argv)
{
	const char *ifname;
	const char *target_ip;
	unsigned int ifindex;
	int fail_threshold = 3;
	int timeout_ms = 1000;
	int interval_sec = 5;
	int fail_count = 0;
	int seq = 0;

	if (argc < 3 || argc > 6) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}

	ifname = argv[1];
	target_ip = argv[2];

	if (argc >= 4)
		fail_threshold = atoi(argv[3]);
	if (argc >= 5)
		timeout_ms = atoi(argv[4]);
	if (argc >= 6)
		interval_sec = atoi(argv[5]);

	ifindex = if_nametoindex(ifname);
	if (ifindex == 0) {
		fprintf(stderr, "if_nametoindex(%s) failed: %s\n",
			ifname, strerror(errno));
		return EXIT_FAILURE;
	}

	printf("[net_monitor] ifname=%s ifindex=%u target=%s\n",
	       ifname, ifindex, target_ip);

	while (1) {
		int carrier = 0;
		int ret;

		if (read_int_sysfs(ifname, "carrier", &carrier) < 0) {
			printf("[net_monitor] carrier unavailable, skip\n");
			sleep(interval_sec);
			continue;
		}

		if (!carrier) {
			printf("[net_monitor] carrier down, wait for physical link\n");
			fail_count = 0;
			sleep(interval_sec);
			continue;
		}

		if (!has_ipv4_address(ifname)) {
			printf("[net_monitor] no IPv4 address, skip restart\n");
			fail_count = 0;
			sleep(interval_sec);
			continue;
		}

		if (!has_default_route(ifname)) {
			printf("[net_monitor] no default route on %s\n", ifname);
		}

		ret = send_ping_once(target_ip, timeout_ms, seq++);
		if (ret == 0) {
			printf("[net_monitor] ping ok\n");
			fail_count = 0;
		} else {
			fail_count++;
			printf("[net_monitor] ping failed ret=%d fail_count=%d/%d\n",
			       ret, fail_count, fail_threshold);
		}

		if (fail_count >= fail_threshold) {
			printf("[net_monitor] request interface restart ifindex=%u\n",
			       ifindex);

			ret = request_restart(ifindex);
			if (ret < 0)
				printf("[net_monitor] restart request failed: %s\n",
				       strerror(-ret));

			fail_count = 0;
			sleep(interval_sec * 2);
		}

		sleep(interval_sec);
	}

	return EXIT_SUCCESS;
}
