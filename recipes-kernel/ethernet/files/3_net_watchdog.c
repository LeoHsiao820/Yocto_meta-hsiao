#include <linux/capability.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#define DRV_NAME "net_watchdog"
#define DEVICE_NAME "net_watchdog"
#define CMD_MAX_LEN 64

static dev_t net_watchdog_devno;
static struct cdev net_watchdog_cdev;
static struct class *net_watchdog_class;
static struct device *net_watchdog_device;
static struct workqueue_struct *net_watchdog_wq;

static unsigned int cooldown_ms = 5000;
module_param(cooldown_ms, uint, 0644);
MODULE_PARM_DESC(cooldown_ms, "Minimum interval between restart requests in milliseconds");

struct restart_context {
	struct work_struct work;
	atomic_t busy;
	int ifindex;
	unsigned long last_request;
};

static struct restart_context restart_ctx;

static void net_watchdog_restart_work(struct work_struct *work)
{
	struct net_device *ndev;
	int ifindex = restart_ctx.ifindex;
	int ret;

	ndev = dev_get_by_index(&init_net, ifindex);
	if (!ndev) {
		pr_err("[%s] ifindex=%d not found\n", DRV_NAME, ifindex);
		goto out;
	}

	pr_info("[%s] restarting interface ifname=%s ifindex=%d via net_device API\n",
		DRV_NAME, ndev->name, ndev->ifindex);

	rtnl_lock();

	if (netif_running(ndev)) {
		dev_close(ndev);
		pr_info("[%s] dev_close(%s) done\n", DRV_NAME, ndev->name);
	} else {
		pr_info("[%s] %s is not running, will try dev_open only\n",
			DRV_NAME, ndev->name);
	}

	ret = dev_open(ndev, NULL);
	if (ret)
		pr_err("[%s] dev_open(%s) failed: %d\n",
		       DRV_NAME, ndev->name, ret);
	else
		pr_info("[%s] restart request completed for %s\n",
			DRV_NAME, ndev->name);

	rtnl_unlock();
	dev_put(ndev);

out:
	atomic_set(&restart_ctx.busy, 0);
}

static int parse_restart_command(char *buf, int *ifindex)
{
	char action[16];
	int parsed_ifindex;
	int ret;

	ret = sscanf(buf, "%15s %d", action, &parsed_ifindex);
	if (ret != 2)
		return -EINVAL;

	if (strcmp(action, "RESTART") && strcmp(action, "RESET"))
		return -EINVAL;

	if (parsed_ifindex <= 0)
		return -EINVAL;

	*ifindex = parsed_ifindex;
	return 0;
}

static ssize_t net_watchdog_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
	char kbuf[CMD_MAX_LEN];
	int ifindex;
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (len == 0 || len >= CMD_MAX_LEN)
		return -EINVAL;

	memset(kbuf, 0, sizeof(kbuf));

	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;

	ret = parse_restart_command(kbuf, &ifindex);
	if (ret) {
		pr_warn("[%s] invalid command: %s\n", DRV_NAME, kbuf);
		return ret;
	}

	if (time_before(jiffies, restart_ctx.last_request + msecs_to_jiffies(cooldown_ms))) {
		pr_warn("[%s] restart request rejected by cooldown\n", DRV_NAME);
		return -EAGAIN;
	}

	if (atomic_cmpxchg(&restart_ctx.busy, 0, 1) != 0) {
		pr_warn("[%s] restart already in progress\n", DRV_NAME);
		return -EBUSY;
	}

	restart_ctx.ifindex = ifindex;
	restart_ctx.last_request = jiffies;

	if (!queue_work(net_watchdog_wq, &restart_ctx.work)) {
		atomic_set(&restart_ctx.busy, 0);
		return -EBUSY;
	}

	pr_info("[%s] queued restart request for ifindex=%d\n", DRV_NAME, ifindex);

	return len;
}

static int net_watchdog_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int net_watchdog_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations net_watchdog_fops = {
	.owner = THIS_MODULE,
	.open = net_watchdog_open,
	.release = net_watchdog_release,
	.write = net_watchdog_write,
};

static int __init net_watchdog_init(void)
{
	int ret;

	atomic_set(&restart_ctx.busy, 0);
	restart_ctx.ifindex = 0;
	restart_ctx.last_request = 0;
	INIT_WORK(&restart_ctx.work, net_watchdog_restart_work);

	net_watchdog_wq = alloc_ordered_workqueue(DRV_NAME, 0);
	if (!net_watchdog_wq)
		return -ENOMEM;

	ret = alloc_chrdev_region(&net_watchdog_devno, 0, 1, DEVICE_NAME);
	if (ret)
		goto err_wq;

	cdev_init(&net_watchdog_cdev, &net_watchdog_fops);
	ret = cdev_add(&net_watchdog_cdev, net_watchdog_devno, 1);
	if (ret)
		goto err_chrdev;

	net_watchdog_class = class_create(DEVICE_NAME);
	if (IS_ERR(net_watchdog_class)) {
		ret = PTR_ERR(net_watchdog_class);
		goto err_cdev;
	}

	net_watchdog_device = device_create(net_watchdog_class, NULL, net_watchdog_devno, NULL, DEVICE_NAME);
	if (IS_ERR(net_watchdog_device)) {
		ret = PTR_ERR(net_watchdog_device);
		goto err_class;
	}

	pr_info("[%s] loaded: /dev/%s major=%d minor=%d\n",
		DRV_NAME, DEVICE_NAME,
		MAJOR(net_watchdog_devno), MINOR(net_watchdog_devno));
	return 0;

err_class:
	class_destroy(net_watchdog_class);
err_cdev:
	cdev_del(&net_watchdog_cdev);
err_chrdev:
	unregister_chrdev_region(net_watchdog_devno, 1);
err_wq:
	destroy_workqueue(net_watchdog_wq);
	return ret;
}

static void __exit net_watchdog_exit(void)
{
	flush_workqueue(net_watchdog_wq);
	device_destroy(net_watchdog_class, net_watchdog_devno);
	class_destroy(net_watchdog_class);
	cdev_del(&net_watchdog_cdev);
	unregister_chrdev_region(net_watchdog_devno, 1);
	destroy_workqueue(net_watchdog_wq);

	pr_info("[%s] unloaded\n", DRV_NAME);
}

module_init(net_watchdog_init);
module_exit(net_watchdog_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hsiao");
MODULE_DESCRIPTION("User-space controlled net_device restart watchdog");
MODULE_VERSION("1.0");