#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/container_of.h>
#include <linux/delay.h>

/* * 設備硬體參數定義
 * AT24C256 (256K bit) = 32768 Bytes
 * Page Size 64 Bytes 是寫入時最重要的限制，跨頁寫入會導致地址捲回 (Roll-over)
 */
#define DEVICE_NAME "at24c256" 
#define EEPROM_SIZE 32768 
#define PAGE_SIZE 64 

/* * 第一部分：私有數據結構 (Private Data Structure)
 * 此結構體是驅動程式的「心臟」，封裝了所有執行期間需要的狀態與物件。
 * 在 Linux 驅動開發中，我們會將此結構掛載在 file->private_data 中傳遞。
 */
struct at24_data {
    struct i2c_client *client;  /* 代表對應的 I2C 從設備控制柄 */
    struct cdev cdev;           /* 字元設備結構，處理檔案系統介面 */
    struct class *class;        /* 用於在 /sys/class 建立目錄 */
    struct device *device;      /* 用於在 /dev 建立設備節點 */
    struct mutex lock;          /* 互斥鎖：防止多個 Process 同時讀寫 I2C 導致匯流排衝突 */
    dev_t dev_id;               /* 設備編號 (包含 Major 與 Minor number) */
    unsigned int size;          /* 總容量 */
    unsigned int page_size;     /* 頁大小 */
    loff_t offset;              /* 當前讀寫位置 */
};

/* * 第二部分：I2C 讀取實作
 * AT24C256 讀取流程：
 * 1. Dummy Write: 先寫入 2-byte 的記憶體地址。
 * 2. Repeated Start: 不發送 Stop，直接發送 Start + Read 位元。
 * 3. Read Data: 連續讀取所需的字節數。
 */
static ssize_t at24_eeprom_read(struct file *filp, char __user *buf, size_t count, loff_t *offset)
{
    int ret;
    struct at24_data *priv = filp->private_data;
    struct i2c_client *client = priv->client;
    struct i2c_msg msg[2];      /* 使用兩個訊息組合成一個交易 (Transaction) */
    __u8 addr_buf[2];           /* 存放 16-bit 地址 */
    __u8 *data_buf;

    /* 邊界安全性檢查：防止讀取越界 */
    if (*offset < 0 || *offset >= priv->size) {
        return -EINVAL;
    }
    if (*offset + count > priv->size) {
        count = priv->size - *offset;
    }

    /* 在核心空間配置暫存區，避免直接在 Mutex 內進行耗時的 copy_to_user */
    data_buf = kmalloc(count, GFP_KERNEL);
    if (!data_buf) {       
        return -ENOMEM;
    }

    mutex_lock(&priv->lock); 

    /* * 第一個訊息：寫入目標地址 (16-bit)
     * 注意：EEPROM 要求 High Byte 先發送 (Big-endian)
     */
    addr_buf[0] = (*offset >> 8) & 0xFF; 
    addr_buf[1] = *offset & 0xFF;        

    msg[0].addr = client->addr;
    msg[0].flags = 0;           /* 0 表示 Write */
    msg[0].len = 2;
    msg[0].buf = addr_buf;

    /* 第二個訊息：讀取數據 */
    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;    /* 設定讀取標誌 */
    msg[1].len = count;
    msg[1].buf = data_buf;

    /* 執行 I2C 傳輸，i2c_transfer 會在訊息間自動插入 Restart 訊號 */
    ret = i2c_transfer(client->adapter, msg, 2);
    mutex_unlock(&priv->lock);

    if (ret < 0) {
        kfree(data_buf);
        return ret;
    }

    /* 將讀取到的核心數據安全地拷貝至使用者空間 */
    if (copy_to_user(buf, data_buf, count)) {
        kfree(data_buf);
        return -EFAULT;
    }

    kfree(data_buf);
    *offset += count;           /* 更新檔案指標位置 */
    return count; 
}

/* * 第三部分：I2C 寫入實作 (包含 Page Write 邏輯)
 * 重要：EEPROM 寫入不能單純的一次丟出去。
 * 1. 物理頁限制：一次寫入最多 64 bytes，且不可跨越頁邊界（例如從 63 寫到 64 會失敗）。
 * 2. 寫入延遲：寫完後晶片需要時間固化資料 (Internal Write Cycle)，通常需 msleep(5)。
 */
static ssize_t at24_eeprom_write(struct file *filp, const char __user *buf, size_t count, loff_t *offset)
{
    int ret = 0;
    struct at24_data *priv = filp->private_data;
    struct i2c_client *client = priv->client;
    __u8 *write_buf;
    size_t total_written = 0;
    
    if (*offset >= priv->size) {
        return -EFBIG;
    }
    if (*offset + count > priv->size) {
        count = priv->size - *offset;
    }

    /* 配置緩衝區：2-byte 地址 + 最大 64-byte 數據 */
    write_buf = kmalloc(2 + priv->page_size, GFP_KERNEL);
    if (!write_buf) {
        return -ENOMEM;
    }

    /* 迴圈處理：將大塊資料拆解為符合 Page Write 規範的小區塊 */
    while (total_written < count) {
        /* 計算當前地址距離下一頁開頭還有多少空間 */
        size_t page_offset = (*offset + total_written) % priv->page_size;
        /* 決定本次寫入長度：不可超過頁邊界，也不可超過剩餘待寫入長度 */
        size_t bytes_to_write = min((size_t)(priv->page_size - page_offset), count - total_written);

        mutex_lock(&priv->lock); 

        /* 準備資料包：[Addr High][Addr Low][Data0][Data1]... */
        write_buf[0] = ((*offset + total_written) >> 8) & 0xFF; 
        write_buf[1] = (*offset + total_written) & 0xFF;        

        if (copy_from_user(&write_buf[2], buf + total_written, bytes_to_write)) {
            mutex_unlock(&priv->lock);
            kfree(write_buf);
            return -EFAULT;
        }

        struct i2c_msg msg = {
            .addr = client->addr,
            .flags = 0, 
            .len = 2 + bytes_to_write,
            .buf = write_buf,
        };

        ret = i2c_transfer(client->adapter, &msg, 1);
        mutex_unlock(&priv->lock);

        if (ret < 0) {
            kfree(write_buf);
            return ret;
        }

        total_written += bytes_to_write;
        *offset += bytes_to_write;

        /* 關鍵：等待 EEPROM 完成內部物理寫入，否則下一次命令會被 NACK */
        msleep(5); 
    }
    kfree(write_buf);

    return total_written;
}

/* * 第四部分：Lseek 實作
 * 讓使用者可以使用 lseek() 來隨機存取 EEPROM 內的任何位置。
 */
static loff_t at24_eeprom_llseek(struct file *file, loff_t offset, int whence)
{
    loff_t ret = -EINVAL;
    struct at24_data *priv = file->private_data;

    switch (whence) {
    case SEEK_SET: /* 相對於檔案開頭 */
        if (offset >= 0 && offset < priv->size) {
            file->f_pos = offset;
            ret = file->f_pos;
        }
        break;
    case SEEK_CUR: /* 相對於當前位置 */
        if (file->f_pos + offset >= 0 && file->f_pos + offset < priv->size) {
            file->f_pos += offset;
            ret = file->f_pos;
        }
        break;
    case SEEK_END: /* 相對於檔案結尾 */
        if (offset <= 0 && offset > -(priv->size)) {
            file->f_pos = priv->size + offset;
            ret = file->f_pos;
        }
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}

/* * 第五部分：字元設備操作接口定義
 * 連結檔案系統呼叫 (Open/Read/Write) 與驅動實作函式。
 */
static int at24_open(struct inode *inode, struct file *filp)
{
    /* 使用 container_of 透過 cdev 指標找回整個 at24_data 結構體的起始位址 */
    struct at24_data *priv = container_of(inode->i_cdev, struct at24_data, cdev);
    filp->private_data = priv; /* 儲存至私有資料區，供 read/write 使用 */

    return 0;
}

static const struct file_operations at24_fops = {
    .owner  = THIS_MODULE,
    .open   = at24_open,
    .read   = at24_eeprom_read,  
    .write  = at24_eeprom_write,
    .llseek = at24_eeprom_llseek, 
};

/* * 第六部分：Probe 函式 (設備匹配成功後執行)
 * 當 Device Tree 的 compatible 字串與此驅動匹配時，核心會呼叫此函式。
 * 負責：初始化硬體、申請資源 (Major/Minor ID)、建立 /dev 節點。
 */
static int at24_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct at24_data *priv;
    int ret;
    
    dev_info(dev, "AT24C256 probing started...\n");

    /* 分配私有資料空間，devm_ 開頭的函式會在驅動解除時自動釋放記憶體 */
    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    /* 初始化狀態與互斥鎖 */
    priv->client = client;
    priv->size = EEPROM_SIZE;      
    priv->page_size = PAGE_SIZE;  
    mutex_init(&priv->lock);
    i2c_set_clientdata(client, priv);

    /* 1. 向系統申請字元設備編號 */
    ret = alloc_chrdev_region(&priv->dev_id, 0, 1, "at24c256");
    if (ret < 0) {
        dev_err(dev, "Failed to allocate char device region\n");
        return ret;
    }

    /* 2. 初始化 cdev 並連結 file_operations */
    cdev_init(&priv->cdev, &at24_fops);
    ret = cdev_add(&priv->cdev, priv->dev_id, 1);
    if (ret < 0) {
        dev_err(dev, "Failed to add cdev\n");
        goto unreg_region;
    }

    /* 3. 在 /sys/class 下建立類別，這是自動建立 /dev 節點的前提 */
    priv->class = class_create("at24c256_class");
    if (IS_ERR(priv->class)) {
        dev_err(dev, "Failed to create class\n");
        ret = PTR_ERR(priv->class);
        goto del_cdev;
    }

    /* 4. 真正建立 /dev/at24c256 設備檔案 */
    priv->device = device_create(priv->class, NULL, priv->dev_id, NULL, "at24c256");
    if (IS_ERR(priv->device)) {
        dev_err(dev, "Failed to create device\n");
        ret = PTR_ERR(priv->device);
        goto destroy_class;
    }

    dev_info(dev, "AT24C256 driver registered at /dev/%s\n", DEVICE_NAME);
    return 0;

/* 錯誤處理路徑：標籤順序與申請順序相反 */
destroy_class:
    class_destroy(priv->class);
del_cdev:
    cdev_del(&priv->cdev);
unreg_region:
    unregister_chrdev_region(priv->dev_id, 1);
    return ret;
}

/* * 第七部分：Remove 函式 (驅動卸載或設備拔除時執行)
 * 負責清理 Probe 時申請的所有資源，避免核心記憶體洩漏。
 */
static void at24_remove(struct i2c_client *client)
{
    struct at24_data *priv = i2c_get_clientdata(client);
    struct device *dev = &client->dev;

    device_destroy(priv->class, priv->dev_id);
    class_destroy(priv->class);
    cdev_del(&priv->cdev);
    unregister_chrdev_region(priv->dev_id, 1);

    dev_info(dev, "AT24C256 driver removed\n");
}

/* * 第八部分：匹配表定義
 * 這是連動 Device Tree 的關鍵，核心會尋找 compatible = "hsiao,at24c256" 的節點。
 */
static const struct of_device_id at24_of_match[] = {
    { .compatible = "hsiao,at24c256" },
    { }
};
MODULE_DEVICE_TABLE(of, at24_of_match);

/* 驅動程式主體結構 */
static struct i2c_driver at24_driver = {
    .driver = {
        .name = "at24c256_driver",
        .of_match_table = at24_of_match,
    },
    .probe    = at24_probe,
    .remove   = at24_remove,
};

/* 核心巨集：自動生成模組進入與退出函式 */
module_i2c_driver(at24_driver);

MODULE_AUTHOR("Leo <littlepig602@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AT24C256 I2C EEPROM Driver Framework");