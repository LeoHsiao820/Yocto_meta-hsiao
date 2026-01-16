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
#include <linux/platform_device.h>

/* Structure Definition */
struct led_gpio {
    struct gpio_desc *gpiod;
};

struct led_gpio gpioled;


static int gpioled_probe(struct platform_device *pdev)
{
    u32 out_value;

    


    return 0;
}

static void gpioled_remove(struct platform_device *pdev)
{
    
}

/* Match table for device tree binding */
static const struct of_device_id gpioled_of_match[] = {
	{ .compatible = "hsiao,gpioleddev", .data = NULL},
	{ }, // Sentinel
};

/* Structure Definition */
static struct platform_driver gpioled_platform_driver = {
    .probe  = gpioled_probe,      // 匹配成功時執行
    .remove = gpioled_remove,     // 驅動卸載時執行
    .driver = {
        .name           = "gpioled_platform_driver",
        .of_match_table = gpioled_of_match, // 關聯上面的匹配表
    },
};


static int __init gpioled_dev_init(void)
{
	int ret;

    ret = platform_driver_register(&gpioled_platform_driver);
    if (ret) {
        pr_err("%s Failed to register gpioled platform driver: %d\n", __func__, ret);
        return ret;
    }

    pr_info("%s: gpioled platform driver registered successfully\n", __func__);
    return 0;
}

static void __exit gpioled_dev_exit(void)
{
	platform_driver_unregister(&gpioled_platform_driver);
    pr_info("%s: gpioled platform driver unregistered successfully\n", __func__);   
}


/* Module Definition */
module_init(gpioled_dev_init)
module_exit(gpioled_dev_exit)

MODULE_AUTHOR("Leo <littlepig602@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gpio_led module");

