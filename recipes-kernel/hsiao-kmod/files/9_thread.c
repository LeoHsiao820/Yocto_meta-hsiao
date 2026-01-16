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
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/types.h>
#include <linux/semaphore.h>


/* MACRO */
#define GPIOLED_MAJOR 	    (200)
#define GPIOLED_NAME 	    ("gpioled")
#define GPIOLED_COUNT		(1)

#define READ_BUF_SIZE		(256)
#define WRITE_BUF_SIZE		(256)

#define TRUE    			(0)
#define FALSE   			(1)

#define LED_ON				(0)
#define LED_OFF				(1)

//Thread types 
#define SPIN_LOCK 			(0)
#define SEMAPHORE 			(1)
#define MUTEX	 			(2)


/* Variables */
static __u16 write_buf[WRITE_BUF_SIZE];
static __u16 default_thread_type = MUTEX;  //Default thread type is mutex


/* Funtion Declaration */
static int Led_Open(struct inode *inode, struct file *file);
static int Led_Release(struct inode *inode, struct file *file);
static ssize_t Led_Read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
static ssize_t Led_Write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);

/* Structure Definition */
static struct file_operations gpioled_fops = {
	.owner	 = 	THIS_MODULE,
	.open	 =	Led_Open,
	.release =	Led_Release,
	.read	 =	Led_Read,
	.write	 =	Led_Write,
};

struct gpioled_dev { 
	dev_t dev_id; 
	struct cdev cdev; 
	struct class *class; 
	struct device *device; 
	int major; 
	int minor; 
	struct device_node *nd; 
	int led_gpio; 
	int dev_status;
	spinlock_t lock;
	struct semaphore sem;
	struct mutex mtx;
};

static struct gpioled_dev gpioled;


/* Funtion Implementation */
static int __init Led_Init(void)
{
	int ret = TRUE;

	/* Allocate device number */
	if (gpioled.major != 0) //The device major number is defined
	{	
		gpioled.dev_id = MKDEV(gpioled.major, gpioled.minor);
		ret = register_chrdev_region(gpioled.dev_id, GPIOLED_COUNT, GPIOLED_NAME);
		if (ret != TRUE) 
		{
			printk("Gpio_led: unable to register device\n");
			return ret;
		}
	}
	else //The device major number is not defined
	{
		ret = alloc_chrdev_region(&gpioled.dev_id, 0, GPIOLED_COUNT, GPIOLED_NAME);
		if (ret != TRUE) 
		{
			printk("Gpio_led: unable to allocate device region\n");
			return ret;
		}
		gpioled.major = MAJOR(gpioled.dev_id);
		gpioled.minor = MINOR(gpioled.dev_id);
	}
	printk("Major device: %d, Minor device: %d\n", gpioled.major, gpioled.minor);

	/* GPIO_led device structure initialization and add to system */
	gpioled.cdev.owner = THIS_MODULE;
	cdev_init(&gpioled.cdev, &gpioled_fops);
	ret = cdev_add(&gpioled.cdev, gpioled.dev_id, GPIOLED_COUNT);
	if (ret != TRUE) 
	{
		printk("Gpio_led: unable to add gpio_led to system\n");
		return ret;
	}

	/* Create class */
	gpioled.class = class_create(GPIOLED_NAME);
	if (IS_ERR(gpioled.class)) 
	{
		printk("Gpio_led: unable to create class\n");
	}

	/* Create device node */
	gpioled.device = device_create(gpioled.class, NULL, gpioled.dev_id, NULL, GPIOLED_NAME);
	if (IS_ERR(gpioled.device)) 
	{
		printk("Gpio_led: unable to create device\n");
	}
	else
	{
		printk("Device node created successfully\n");
	}

	/* Set GPIO_led node from device tree */ 
    gpioled.nd = of_find_node_by_path("/gpioled"); 
    if(gpioled.nd == NULL) 
    { 
        printk("gpioled node can't be found!\r\n"); 
        return -EINVAL; 
    } 
    else 
    { 
        printk("gpioled node has been found!\r\n"); 
    } 
        
    /* Get led number from the property of gpio_led */ 
    gpioled.led_gpio = of_get_named_gpio(gpioled.nd, "led-gpio", 0); 
    if(gpioled.led_gpio < 0) 
    { 
        printk("can't get led-gpio"); 
        return -EINVAL; 
    } 
    printk("led-gpio num = %d\r\n", gpioled.led_gpio);

	/* Set GPIO output */
	ret = gpio_direction_output(gpioled.led_gpio, 1); 
	if(ret != TRUE) 
	{ 
		printk("can't set gpio!\r\n"); 
		return EINVAL; 
	}

	/* Initialize the thread */
	if (default_thread_type == SPIN_LOCK)
	{
		spin_lock_init(&gpioled.lock);
	}
	else if (default_thread_type == SEMAPHORE)
	{
		sema_init(&gpioled.sem, 1);
	}
	else if (default_thread_type == MUTEX)
	{
		mutex_init(&gpioled.mtx);
	}

	return ret;
}

static void __exit Led_Exit(void)
{
	/* Free GPIO */
	gpio_free(gpioled.led_gpio);

	/* Destroy class */
	class_destroy(gpioled.class);

	/* Destroy device node */
	device_destroy(gpioled.class, gpioled.dev_id);

	/* Delete the device from system */
	cdev_del(&gpioled.cdev);

	/* Unregister device number */
	unregister_chrdev_region(gpioled.dev_id, GPIOLED_COUNT);


	printk("Gpio_led device exit\n");
}

/* 
 * @description : 打开设备 
 * @param – inode : 传递给驱动的inode 
 * @param – file : 设备文件，file结构体有个叫做private_data的成员变量，一般在open的时候将private_data指向设备结构体。 
 * @return : 0 成功;其他 失败 
 * */
static int Led_Open(struct inode *inode, struct file *file)
{
	__u32 irq_flag;
	file->private_data = &gpioled;  /* 设置私有数据 */

	if (default_thread_type == SPIN_LOCK)
	{
		spin_lock_irqsave(&gpioled.lock, irq_flag); //Acquire lock
		if (gpioled.dev_status) 
		{ 
			spin_unlock_irqrestore(&gpioled.lock, irq_flag); //Release lock
			printk("Gpio_led device is busy!\n");
			return -EBUSY; 
		}
		gpioled.dev_status++;
		spin_unlock_irqrestore(&gpioled.lock, irq_flag); //Release lock
	}
	else if (default_thread_type == SEMAPHORE)
	{
		if (down_interruptible(&gpioled.sem) != 0)
		{
			printk("Gpio_led: could not lock device during open\n");
			return -EBUSY;
		}
	}
	else if (default_thread_type == MUTEX)
	{
		if (mutex_lock_interruptible(&gpioled.mtx) != 0)
		{
			printk("Gpio_led: could not lock device during open\n");
			return -EBUSY;
		}
	}	

	return TRUE;
}

static int Led_Release(struct inode *inode, struct file *file)
{
	__u32 irq_flag;
	
	if (default_thread_type == SPIN_LOCK)
	{
		spin_lock_irqsave(&gpioled.lock, irq_flag); //Acquire lock
		if (gpioled.dev_status == 0) 
		{ 
			spin_unlock_irqrestore(&gpioled.lock, irq_flag); //Release lock
			printk("Gpio_led device is not opened!\n");
			return -EBUSY; 
		}
		gpioled.dev_status--;
		spin_unlock_irqrestore(&gpioled.lock, irq_flag); //Release lock
	}
	else if (default_thread_type == SEMAPHORE)
	{
		up(&gpioled.sem);
	}
	else if (default_thread_type == MUTEX)
	{
		mutex_unlock(&gpioled.mtx);
	}	

	return TRUE;
}

static ssize_t Led_Read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	int ret = TRUE;


	return TRUE;
}

static ssize_t Led_Write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	int ret = TRUE;
	__u16 led_status;

	/* Copy data from user space to kernel space */
	ret = copy_from_user(write_buf, buf, len);
	if (ret != 0)
	{
		printk("Gpio_led: copy from user failed\n");
		return -EFAULT;
	}

	/* Get LED status from write_buf */
	led_status = write_buf[0];
	if(led_status == LED_ON)
	{
		gpio_set_value(gpioled.led_gpio, 0); //LED ON
	}
	else if(led_status == LED_OFF)
	{
		gpio_set_value(gpioled.led_gpio, 1); //LED OFF
	}
	else
	{
		printk("Invalid LED status value!\n");
		return -EINVAL;
	}

	return TRUE;
}


/* Module Definition */
module_init(Led_Init)
module_exit(Led_Exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gpio_led module");

