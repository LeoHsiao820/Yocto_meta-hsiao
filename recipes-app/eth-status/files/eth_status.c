#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef IF_NAMESIZE
#define IF_NAMESIZE 16
#endif

#define SYSFS_PATH_MAX 256
#define TEXT_BUF_SIZE 256

static int read_first_line(const char *path, char *buf, size_t size)
{
    FILE *fp;
    size_t len;

    if (!buf || size == 0)
        return -EINVAL;

    fp = fopen(path, "r");
    if (!fp)
        return -errno;

    if (!fgets(buf, size, fp)) {
        int ret = ferror(fp) ? -errno : -ENODATA;
        fclose(fp);
        return ret;
    }

    fclose(fp);

    len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[len - 1] = '\0';
        len--;
    }

    return 0;
}

static void make_sysfs_path(char *path, size_t size, const char *ifname,
                            const char *entry)
{
    snprintf(path, size, "/sys/class/net/%s/%s", ifname, entry);
}

static void print_sysfs_value(const char *label, const char *ifname,
                              const char *entry)
{
    char path[SYSFS_PATH_MAX];
    char value[TEXT_BUF_SIZE];
    int ret;

    make_sysfs_path(path, sizeof(path), ifname, entry);
    ret = read_first_line(path, value, sizeof(value));
    if (ret < 0)
        printf("%-18s: unavailable (%s)\n", label, strerror(-ret));
    else
        printf("%-18s: %s\n", label, value);
}

static void print_driver_name(const char *ifname)
{
    char path[SYSFS_PATH_MAX];
    char target[SYSFS_PATH_MAX];
    ssize_t len;
    const char *driver;

    snprintf(path, sizeof(path), "/sys/class/net/%s/device/driver", ifname);

    len = readlink(path, target, sizeof(target) - 1);
    if (len < 0) {
        printf("%-18s: unavailable (%s)\n", "driver", strerror(errno));
        return;
    }

    target[len] = '\0';
    driver = strrchr(target, '/');
    driver = driver ? driver + 1 : target;

    printf("%-18s: %s\n", "driver", driver);
}

static void print_ipv4_addresses(const char *ifname)
{
    struct ifaddrs *ifaddr;
    struct ifaddrs *ifa;
    int found = 0;

    if (getifaddrs(&ifaddr) < 0) {
        printf("%-18s: unavailable (%s)\n", "ipv4 address", strerror(errno));
        return;
    }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        char addr[INET_ADDRSTRLEN];
        struct sockaddr_in *sin;

        if (!ifa->ifa_addr)
            continue;
        if (strcmp(ifa->ifa_name, ifname) != 0)
            continue;
        if (ifa->ifa_addr->sa_family != AF_INET)
            continue;

        sin = (struct sockaddr_in *)ifa->ifa_addr;
        if (!inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof(addr)))
            continue;

        if (!found)
            printf("%-18s: %s\n", "ipv4 address", addr);
        else
            printf("%-18s  %s\n", "", addr);

        found = 1;
    }

    if (!found)
        printf("%-18s: none\n", "ipv4 address");

    freeifaddrs(ifaddr);
}

static int get_interface_flags(const char *ifname, short *flags)
{
    struct ifreq ifr;
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    memset(&ifr, 0, sizeof(ifr));
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", ifname);

    if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    close(fd);
    *flags = ifr.ifr_flags;
    return 0;
}

static void print_admin_state(const char *ifname)
{
    short flags;
    int ret;

    ret = get_interface_flags(ifname, &flags);
    if (ret < 0) {
        printf("%-18s: unavailable (%s)\n", "admin state", strerror(-ret));
        return;
    }

    printf("%-18s: %s\n", "admin state", (flags & IFF_UP) ? "UP" : "DOWN");

    printf("%-18s: 0x%04x", "interface flags", (unsigned int)flags);

    if (flags & IFF_UP)
    printf(" UP");

    if (flags & IFF_RUNNING)
        printf(" RUNNING");

#ifdef IFF_LOWER_UP
    if (flags & IFF_LOWER_UP)
        printf(" LOWER_UP");
#endif

    if (flags & IFF_BROADCAST)
        printf(" BROADCAST");

    if (flags & IFF_MULTICAST)
        printf(" MULTICAST");

    printf("\n");
}

static void print_default_route_hint(const char *ifname)
{
    FILE *fp;
    char line[TEXT_BUF_SIZE];
    int found = 0;

    fp = fopen("/proc/net/route", "r");
    if (!fp) {
        printf("%-18s: unavailable (%s)\n", "default route", strerror(errno));
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        char iface[IF_NAMESIZE];
        unsigned long destination;

        if (sscanf(line, "%15s %lx", iface, &destination) != 2)
            continue;

        if (strcmp(iface, ifname) == 0 && destination == 0) {
            found = 1;
            break;
        }
    }

    fclose(fp);

    printf("%-18s: %s\n", "default route", found ? "yes" : "no");
}

static void print_rx_tx_stats(const char *ifname)
{
    char path[SYSFS_PATH_MAX];
    char value[TEXT_BUF_SIZE];
    int ret;

    make_sysfs_path(path, sizeof(path), ifname, "statistics/rx_packets");
    ret = read_first_line(path, value, sizeof(value));
    printf("%-18s: %s\n", "rx packets", ret < 0 ? "unavailable" : value);

    make_sysfs_path(path, sizeof(path), ifname, "statistics/tx_packets");
    ret = read_first_line(path, value, sizeof(value));
    printf("%-18s: %s\n", "tx packets", ret < 0 ? "unavailable" : value);

    make_sysfs_path(path, sizeof(path), ifname, "statistics/rx_errors");
    ret = read_first_line(path, value, sizeof(value));
    printf("%-18s: %s\n", "rx errors", ret < 0 ? "unavailable" : value);

    make_sysfs_path(path, sizeof(path), ifname, "statistics/tx_errors");
    ret = read_first_line(path, value, sizeof(value));
    printf("%-18s: %s\n", "tx errors", ret < 0 ? "unavailable" : value);
}

static int interface_exists(const char *ifname)
{
    char path[SYSFS_PATH_MAX];

    snprintf(path, sizeof(path), "/sys/class/net/%s", ifname);
    return access(path, F_OK) == 0;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <interface>\n", prog);
    fprintf(stderr, "Example: %s eth0\n", prog);
}

int main(int argc, char **argv)
{
    const char *ifname;
    unsigned int ifindex;

    if (argc != 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    ifname = argv[1];

    if (strlen(ifname) >= IF_NAMESIZE) {
        fprintf(stderr, "Interface name is too long: %s\n", ifname);
        return EXIT_FAILURE;
    }

    if (!interface_exists(ifname)) {
        fprintf(stderr, "Interface does not exist: %s\n", ifname);
        return EXIT_FAILURE;
    }

    ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "if_nametoindex(%s) failed: %s\n",
                ifname, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("%-18s: %s\n", "interface", ifname);
    printf("%-18s: %u\n", "ifindex", ifindex);

    print_driver_name(ifname);
    print_admin_state(ifname);
    print_sysfs_value("operstate", ifname, "operstate");
    print_sysfs_value("carrier", ifname, "carrier");
    print_sysfs_value("mac address", ifname, "address");
    print_sysfs_value("mtu", ifname, "mtu");
    print_ipv4_addresses(ifname);
    print_default_route_hint(ifname);
    print_rx_tx_stats(ifname);

    return EXIT_SUCCESS;
}