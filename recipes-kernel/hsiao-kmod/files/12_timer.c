#include <linux/module.h>
#include <linux/kernel.h> 
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/fs.h> 
#include <linux/slab.h>  
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/ioctl.h>
#include <linux/timer.h>


/* MACRO */
#define TIMERDEV_MAJOR 	    (200)
#define TIMERDEV_NAME 	    ("timerdev")
#define TIMERDEV_COUNT		(1)
#define LED_ON				(0)
#define LED_OFF				(1)
//timer
#define CLOSE_CMD			(_IO(0xEF, 0x1)) // Close timer
#define OPEN_CMD			(_IO(0xEF, 0x2)) // Open timer
#define SETPERIOD_CMD		(_IOW(0xEF,0x3,unsigned int)) // Set timer period


/* Funtion Declaration */
static int Timer_Open(struct inode *inode, struct file *file)
static long Timer_Unlocked_Ioctl(struct file *filp, unsigned int cmd, unsigned long arg);


/* Structure Definition */
static struct file_operations gpioled_fops = {
	.owner	 = 	THIS_MODULE,
	.open	 =	Timer_Open,
	.unlocked_ioctl = Timer_Unlocked_Ioctl,
};

struct timer_dev { 
	int major; 
	int minor; 
	dev_t dev; 
	struct cdev cdev; 
	struct class *class; 
	struct device *dev; 
	struct device_node *nd;
	int gpio_num; 
	struct gpio_desc *led_gpio_desc; //LED GPIO descriptor 
	struct timer_list timer; //timer structure
	__u32 timer_period;	//default: ms
};

static struct timer_dev timerdev;


/* Funtion Implementation */
static int Led_Init(void)
{
	int ret = 0;

	/* Set GPIO_led node from device tree */ 
    timerdev.nd = of_find_node_by_path("/timerdev"); 
    if(timerdev.nd == NULL) 
    { 
        pr_err("timerdev node can't be found!\r\n"); 
        return -EINVAL; 
    } 
    pr_info("timerdev node has been found!\r\n"); 

	/* Get led number from the property of gpio_led */ 
    timerdev.gpio_num = of_get_named_gpio(timerdev.nd, "led-gpios", 0); 
    if(timerdev.gpio_num < 0) 
    { 
        pr_info("can't get led-gpio"); 
        return -EINVAL; 
    } 
    pr_info("led-gpio num = %d\r\n", timerdev.gpio_num);

	/* Set GPIO output */
	ret = gpio_direction_output(timerdev.gpio_num, 1); 
	if(ret != 0) 
	{ 
		printk("can't set gpio!\r\n"); 
		return EINVAL; 
	}
    pr_info("GPIO Descriptor acquired successfully. Initial state set to High.\n");

	timerdev.led_gpio_desc = gpio_to_desc(timerdev.gpio_num);
	if (!timerdev.led_gpio_desc)
 	{
		pr_err("Failed to convert GPIO number %d to descriptor.\n", timerdev.gpio_num);
        gpio_free(timerdev.gpio_num); 
		return -EFAULT;
	}
	pr_info("GPIO Descriptor acquired successfully (via conversion). Initial state set to High.\n");

	return 0;
}

static void Timer_Callback(struct timer_list *t)
{
	struct timer_dev *dev;

	dev = from_timer(dev, t, timer); 

	pr_info("Timer callback function called.\n");

	if (!dev->led_gpio_desc)
	{
		pr_info("LED GPIO descriptor is NULL.\n");
		return;
	}

	/* Get gpioled descriptor */
	int value = gpiod_get_value(dev->led_gpio_desc);

	/* Toggle LED state */
	gpiod_set_value(dev->led_gpio_desc, !value);

	/* Re-arm the timer */
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timer_period));
}

/* 
 * @description : 打开设备 
 * @param – inode : 传递给驱动的inode 
 * @param – file : 设备文件，file结构体有个叫做private_data的成员变量，一般在open的时候将private_data指向设备结构体。 
 * @return : 0 成功;其他 失败 
 * */
static int Timer_Open(struct inode *inode, struct file *file)
{
	file->private_data = &timerdev;  /* 设置私有数据 */

	return 0;
}

static long Timer_Unlocked_Ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct timer_dev *dev = (struct timer_dev *)filp->private_data;
	long ret = 0;
	__u32 new_period;

	switch(cmd)
	{
		case CLOSE_CMD:
			del_timer_sync(&dev->timer);
			pr_info("Timer closed.\n");
			break;

		case OPEN_CMD:
			mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timer_period));
			pr_info("Timer opened.\n");
			break;

		case SETPERIOD_CMD:
			new_period = (__u32)arg;
            
            // ** 關鍵修正：最小週期保護 (防止 0 或極小值導致高頻率觸發) **
            if (new_period < 100) { 
                pr_warn("Attempted to set period to %u ms. Setting minimum period to 10 ms.\n", new_period);
                new_period = 100;
            }

			dev->timer_period = new_period;
			pr_info("Timer period set to %u ms.\n", dev->timer_period);
			/* Re-arm the timer with new period */
			if (timer_pending(&dev->timer)) 
			{
				mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timer_period));
            }
			break;

		default:
			pr_info("Invalid command.\n");
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int __init Timer_Init(void)
{
	int ret = 0;

	/* Allocate device number */
	ret = alloc_chrdev_region(&timerdev.dev, 0, TIMERDEV_COUNT, TIMERDEV_NAME);
	if (ret != 0) 
	{
		pr_info("Timer_Dev: unable to allocate device region\n");
		return ret;
	}
	timerdev.major = MAJOR(timerdev.dev);
	timerdev.minor = MINOR(timerdev.dev);

	pr_info("Major device: %d, Minor device: %d\n", timerdev.major, timerdev.minor);

	/* Timer device structure initialization and add to system */
	cdev_init(&timerdev.cdev, &gpioled_fops);
	ret = cdev_add(&timerdev.cdev, timerdev.dev, TIMERDEV_COUNT);
	if (ret != 0) 
	{
		pr_info("Timer_Dev: unable to add Timer_Dev to system\n");
		return ret;
	}

	/* Create class */
	timerdev.class = class_create(TIMERDEV_NAME);
	if (IS_ERR(timerdev.class)) 
	{
		pr_info("Timer_Dev: unable to create class\n");
	}

	/* Create device node */
	timerdev.dev = device_create(timerdev.class, NULL, timerdev.dev, NULL, TIMERDEV_NAME);
	if (IS_ERR(timerdev.dev)) 
	{
		pr_info("Timer_Dev: unable to create device\n");
	}
	else
	{
		pr_info("Device node created successfully\n");
	}

	ret = Led_Init();
	if(ret != 0)
	{
		pr_info("Led_Init failed!\n");
		return -EFAULT;
	}

	/* Init timer */
	timer_setup(&timerdev.timer, Timer_Callback, 0);
	timerdev.timer_period = 3000;
	// mod_timer(&timerdev.timer, jiffies + msecs_to_jiffies(timerdev.timer_period)); //default 3000ms

	return 0;
}

static void __exit Timer_Exit(void)
{
	/* Delete timer */
	del_timer_sync(&timerdev.timer);

	/* Free GPIO */
	if (timerdev.gpio_num >= 0) 
    {
		gpio_free(timerdev.gpio_num);
    }

	/* Destroy class */
	class_destroy(timerdev.class);

	/* Destroy device node */
	device_destroy(timerdev.class, timerdev.dev);

	/* Delete the device from system */
	cdev_del(&timerdev.cdev);

	/* Unregister device number */
	unregister_chrdev_region(timerdev.dev, TIMERDEV_COUNT);

	pr_info("Timer_Dev device exit\n");
}

/* Module Definition */
module_init(Timer_Init)
module_exit(Timer_Exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Timer_Dev module");

