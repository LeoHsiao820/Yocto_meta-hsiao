#include <linux/module.h>
#include <linux/kernel.h> 
#include <linux/init.h>
#include <linux/fs.h> 
#include <linux/uaccess.h> 
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stat.h>   


#define CHRDEVBASE_MAJOR 	(200)
#define CHRDEVBASE_NAME 	("chardevbase")
#define READ_BUF_SIZE		(256)
#define WRITE_BUF_SIZE		(256)
#define TRUE    (0)
#define FALSE   (1)


static char read_buf[READ_BUF_SIZE];
static char write_buf[WRITE_BUF_SIZE];
static char kerneldata[] = {"kernel data!"};


static int chrdevbase_open(struct inode *inode, struct file *file)
{
	printk("chrdevbase_open\n");
	return TRUE;
}

static int chrdevbase_release(struct inode *inode, struct file *file)
{
	//printk("chrdevbase_release\n");
	return TRUE;
}

static ssize_t chrdevbase_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	int ret = TRUE;

	//printk("chrdevbase_read\n");

	memcpy(read_buf, kerneldata, sizeof(kerneldata));
	ret = copy_to_user(buf, read_buf, len);
	if (ret != TRUE) {
		printk("chrdevbase_read: copy_to_user failed\n");
		return FALSE;
	}

	return TRUE;
}

static ssize_t chrdevbase_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	int ret = TRUE;

	//printk("chrdevbase_write\n");

	ret = copy_from_user(write_buf, buf, len);
	if (ret != TRUE) {
		printk("chrdevbase_write: copy_from_user failed\n");
		return FALSE;
	}

	return TRUE;
}

static struct file_operations chrdevbase_fops = {
	.owner	 = 	THIS_MODULE,
	.open	 =	chrdevbase_open,
	.release =	chrdevbase_release,
	.read	 =	chrdevbase_read,
	.write	 =	chrdevbase_write,
};


static int __init Char_Dev_Init(void)
{
	int ret = TRUE;

	printk("Char device start\n");
	
	ret = register_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME, &chrdevbase_fops);
	if (ret != TRUE) {
		printk("Char_Dev_Init: unable to register device\n");
		ret = FALSE;
		return ret;
	}
	else {
		printk("Char_Dev_Init: register device success\n");
	}

	return ret;
}

static void __exit Char_Dev_Exit(void)
{
	unregister_chrdev(CHRDEVBASE_MAJOR, CHRDEVBASE_NAME);
	printk("Char device exit\n");
}

module_init(Char_Dev_Init)
module_exit(Char_Dev_Exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A simple character device test module");

