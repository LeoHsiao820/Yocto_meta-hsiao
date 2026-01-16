#include <linux/module.h>
#include <linux/kernel.h> 
#include <linux/init.h>
#include <linux/fs.h> 
#include <linux/uaccess.h> 
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stat.h>   
#include <linux/cdev.h>
#include <linux/device.h>


/* MACRO */
#define CHRDEVBASE_MAJOR 	(200)
#define CHRDEVBASE_NAME 	("chardevbase")
#define CHRDEV_COUNT		(1)
#define READ_BUF_SIZE		(256)
#define WRITE_BUF_SIZE		(256)
#define TRUE    (0)
#define FALSE   (1)


/* Variables */
static char read_buf[READ_BUF_SIZE];
static char write_buf[WRITE_BUF_SIZE];
static char kerneldata[] = {"kernel data!"};


/* Funtion Declaration */
static int chrdevbase_open(struct inode *inode, struct file *file);
static int chrdevbase_release(struct inode *inode, struct file *file);
static ssize_t chrdevbase_read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
static ssize_t chrdevbase_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);


/* Structure Definition */
static struct file_operations chrdevbase_fops = {
	.owner	 = 	THIS_MODULE,
	.open	 =	chrdevbase_open,
	.release =	chrdevbase_release,
	.read	 =	chrdevbase_read,
	.write	 =	chrdevbase_write,
};

struct char_dev_info {
	struct cdev char_cdev;
	struct class *char_class;
	struct device *char_device;
	dev_t dev_id;
	int major;
	int minor;
};

static struct char_dev_info cdevinfo;


/* Funtion Implementation */
static int __init Char_Dev_Init(void)
{
	int ret = TRUE;

	printk("Char device start\n");
	
	/* Allocate device number */
	if (cdevinfo.major != 0) //The device major number is defined
	{	
		cdevinfo.dev_id = MKDEV(cdevinfo.major, cdevinfo.minor);
		ret = register_chrdev_region(cdevinfo.dev_id, CHRDEV_COUNT, CHRDEVBASE_NAME);
		if (ret != TRUE) 
		{
			printk("Char_Dev_Init: unable to register device\n");
			return ret;
		}
	}
	else //The device major number is not defined
	{
		ret = alloc_chrdev_region(&cdevinfo.dev_id, 0, CHRDEV_COUNT, CHRDEVBASE_NAME);
		if (ret != TRUE) 
		{
			printk("Char_Dev_Init: unable to allocate device region\n");
			return ret;
		}
		cdevinfo.major = MAJOR(cdevinfo.dev_id);
		cdevinfo.minor = MINOR(cdevinfo.dev_id);
	}
	printk("Major device: %d, Minor device: %d\n", cdevinfo.major, cdevinfo.minor);

	/* Char device structure initialization and add to system */
	cdevinfo.char_cdev.owner = THIS_MODULE;
	cdev_init(&cdevinfo.char_cdev, &chrdevbase_fops);
	ret = cdev_add(&cdevinfo.char_cdev, cdevinfo.dev_id, CHRDEV_COUNT);
	if (ret != TRUE) 
	{
		printk("Char_Dev_Init: unable to add char device to system\n");
		return ret;
	}

	/* Create class */
	cdevinfo.char_class = class_create(CHRDEVBASE_NAME);
	if (IS_ERR(cdevinfo.char_class)) 
	{
		printk("Char_Dev_Init: unable to create class\n");
	}

	/* Create device node */
	cdevinfo.char_device = device_create(cdevinfo.char_class, NULL, cdevinfo.dev_id, NULL, CHRDEVBASE_NAME);
	if (IS_ERR(cdevinfo.char_device)) 
	{
		printk("Char_Dev_Init: unable to create device\n");
	}
	else
	{
		printk("Device node created successfully\n");
	}

	return ret;
}

static void __exit Char_Dev_Exit(void)
{
	/* Destroy class */
	class_destroy(cdevinfo.char_class);

	/* Destroy device node */
	device_destroy(cdevinfo.char_class, cdevinfo.dev_id);

	/* Delete char device from system */
	cdev_del(&cdevinfo.char_cdev);

	/* Unregister device number */
	unregister_chrdev_region(cdevinfo.dev_id, CHRDEV_COUNT);


	printk("Char device exit\n");
}

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


/* Module Definition */
module_init(Char_Dev_Init)
module_exit(Char_Dev_Exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A simple character device test module");

