#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define DRV_NAME "net_event_monitor"

static const char *netdev_event_name(unsigned long event)
{
	switch (event) {
	case NETDEV_UP:
		return "NETDEV_UP";
	case NETDEV_DOWN:
		return "NETDEV_DOWN";
	case NETDEV_CHANGE:
		return "NETDEV_CHANGE";
	case NETDEV_REGISTER:
		return "NETDEV_REGISTER";
	case NETDEV_UNREGISTER:
		return "NETDEV_UNREGISTER";
	case NETDEV_CHANGENAME:
		return "NETDEV_CHANGENAME";
	case NETDEV_CHANGEADDR:
		return "NETDEV_CHANGEADDR";
	case NETDEV_GOING_DOWN:
		return "NETDEV_GOING_DOWN";
	case NETDEV_CHANGEUPPER:
		return "NETDEV_CHANGEUPPER";
	default:
		return "NETDEV_UNKNOWN";
	}
}

static void print_netdev_state(struct net_device *ndev, unsigned long event)
{
	if (!ndev)
		return;

	pr_info("[%s] event=%s(%lu), ifname=%s, ifindex=%d, running=%d, carrier=%d, operstate=%u, mtu=%u, mac=%pM\n",
		DRV_NAME,
		netdev_event_name(event),
		event,
		ndev->name,
		ndev->ifindex,
		netif_running(ndev),
		netif_carrier_ok(ndev),
		ndev->operstate,
		ndev->mtu,
		ndev->dev_addr);
}

static int net_event_notifier_cb(struct notifier_block *nb,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev;

	ndev = netdev_notifier_info_to_dev(ptr);
	if (!ndev)
		return NOTIFY_DONE;

	print_netdev_state(ndev, event);

	return NOTIFY_DONE;
}

static struct notifier_block net_event_nb = {
	.notifier_call = net_event_notifier_cb,
};

static int __init net_event_monitor_init(void)
{
	int ret;

	ret = register_netdevice_notifier(&net_event_nb);
	if (ret) {
		pr_err("[%s] failed to register netdevice notifier: %d\n",
		       DRV_NAME, ret);
		return ret;
	}

	pr_info("[%s] loaded: monitoring net_device events\n", DRV_NAME);
	return 0;
}

static void __exit net_event_monitor_exit(void)
{
	unregister_netdevice_notifier(&net_event_nb);
	pr_info("[%s] unloaded\n", DRV_NAME);
}

module_init(net_event_monitor_init);
module_exit(net_event_monitor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hsiao");
MODULE_DESCRIPTION("Linux net_device event monitor for Ethernet learning");
MODULE_VERSION("1.0");