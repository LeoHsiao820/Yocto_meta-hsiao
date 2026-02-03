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

/* * 第一部分：定義私有數據結構 
 * 建議：不要使用全域變數，將所有設備相關的變數封裝在這裡
 */
struct my_key_data {
    struct input_dev *input;
    struct gpio_desc *gpiod;
    int irq;
    unsigned int key_code;
};

/* * 第二部分：中斷處理函式 (ISR)
 * 當按鍵按下時，核心會跳進這裡。
 * 實作重點：
 * 1. 讀取目前的 GPIO 電位狀態。
 * 2. 使用 input_report_key() 上報狀態 (1為按下, 0為放開)。
 * 3. 使用 input_sync() 告訴系統上報完畢。
 */
static irqreturn_t my_key_isr(int irq, void *dev_id)
{
    struct my_key_data *priv = dev_id;
    int state;

    /* [在此實作] 讀取 GPIO 並上報事件 */

    return IRQ_HANDLED;
}

/* * 第三部分：Probe 函式 (設備匹配時執行)
 * 這是驅動程式的心臟，你需要依序完成：
 */
static int my_key_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct my_key_data *priv;
    int ret;

    /* 1. 分配私有數據結構記憶體 (使用 devm_kzalloc) */
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv) return -ENOMEM;

    /* 2. 從 Device Tree 獲取 GPIO 描述字 (使用 devm_gpiod_get) */
    /* [在此實作] */

    /* 3. 配置 Input Device 對象 (使用 devm_input_allocate_device) */
    /* [在此實作] */

    /* 4. 設定 Input Device 的能力 (EV_KEY, BTN_0 等) */
    /* [在此實作] */

    /* 5. 獲取中斷號碼並註冊中斷處理函式 (gpiod_to_irq + devm_request_irq) */
    /* 注意：傳遞 priv 作為 dev_id 以便在 ISR 中存取數據 */
    /* [在此實作] */

    /* 6. 正式註冊 Input Device (input_register_device) */
    /* [在此實作] */

    /* 將私有數據存入 platform_device 以便移除時使用 */
    platform_set_drvdata(pdev, priv);

    return 0;
}

/* * 第四部分：驅動基礎設定與匹配表
 */
static const struct of_device_id my_key_ids[] = {
    { .compatible = "my-project,simple-key" },
    { }
};
MODULE_DEVICE_TABLE(of, my_key_ids);

static struct platform_driver key_input_driver = {
    .probe = my_key_probe,
    .driver = {
        .name = "my_simple_key",
        .of_match_table = my_key_ids,
    },
};

/* Module Definition */
module_platform_driver(key_input_driver);

MODULE_AUTHOR("Leo <littlepig602@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A step-by-step Input Driver Template");

