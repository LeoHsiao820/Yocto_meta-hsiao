#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/err.h>

/* LPUART 關鍵暫存器偏移量 (參考 i.MX93 datasheet) */
#define LPUART_BAUD    0x10  /* Baudrate (BAUD) */
#define LPUART_STAT    0x14  /* Status (STAT) */
#define LPUART_CTRL    0x18  /* Control (CTRL) */
#define LPUART_DATA    0x1C  /* Data (DATA) */

/* Enable bits */
#define CTRL_TE        (1 << 19) /* Transmit Enable */
#define CTRL_RE        (1 << 18) /* Receive Enable */

/* STAT 暫存器位元定義 */
#define STAT_TDRE      (1 << 23) /* Transmit Data Register Empty */
#define STAT_RDRF      (1 << 21) /* Receive Data Register Full */


/**
 * struct lpuart_dev - 自定義的 LPUART 裝置私有數據結構體
 * * 物件導向思維：將這個硬體實例所需的所有 Linux 內核物件與成員「封裝」在一起。
 * 透過這個結構體，驅動程式的各個函式（open, read, write）都能共享同一個硬體實例的狀態。
 */
struct lpuart_dev {
    void __iomem *base;       /* 映射後的暫存器虛擬基地址（核心透過此指標以 readl/writel 存取硬體暫存器） */
    struct cdev cdev;         /* 內核字元設備結構體（Character Device），代表此設備在內核中的身分 */
    struct class *class;      /* 設備類別指標，用於在 /sys/class/ 建立類別節點 */
    struct device *device;    /* 設備物件指標，用於在 /dev/ 自動生成 my_lpuart 節點 */
    dev_t devid;              /* 設備編號（包含 12-bit Major 主設備號 與 20-bit Minor 次設備號） */
    struct clk *clk;          /* 用來保存時脈指標（從平台驅動 probe 階段獲取的 Clock Handle） */
};


/* =========================================================================
 * --- 基礎 Polling（輪詢）函式實作區 ---
 * =========================================================================
 * 這兩個函式直接對晶片的實體暫存器進行操作，不依賴中斷，完全靠 CPU 死等（Busy-waiting）。
 */

/**
 * lpuart_putc - 發送一個字元到 LPUART 暫存器 (Polling 模式)
 * @uart: 指向私有數據結構體的指標
 * @c: 要發送的字元
 */
static void lpuart_putc(struct lpuart_dev *uart, char c)
{
    /* 1. 等待發送緩衝區空 (Transmit Data Register Empty)
     * readl 讀取 STAT 暫存器，並與 STAT_TDRE 遮罩進行波特率狀態檢查。
     * 如果 TDRE 為 0，代表硬體內部的 FIFO/移位暫存器還是滿的，CPU 必須在這裡打轉死等。
     */
    while (!(readl(uart->base + LPUART_STAT) & STAT_TDRE));

    /* 2. 當硬體空閒（TDRE 變為 1）時，將字元寫入 DATA 暫存器，晶片會自動將其透過實體 TX 腳位序列化發送出去 */
    writel(c, uart->base + LPUART_DATA);
}

/**
 * lpuart_getc - 從 LPUART 暫存器接收一個字元 (Polling 模式)
 * @uart: 指向私有數據結構體的指標
 * @return: 接收到的字元
 */
static char lpuart_getc(struct lpuart_dev *uart)
{
    /* 1. 等待接收緩衝區滿 (Receive Data Register Full)
     * 檢查 STAT_RDRF 位元。如果為 0，代表目前實體 RX 腳位上沒有新的資料流進來，CPU 在此死等。
     */
    while (!(readl(uart->base + LPUART_STAT) & STAT_RDRF));

    /* 2. 當 RDRF 變為 1 時，代表硬體已成功接收 1 Byte 數據。
     * 讀取 DATA 暫存器並與 0xFF 做 AND 運算（只取低 8 位資料位），然後轉型為 char 回傳。
     */
    return (char)(readl(uart->base + LPUART_DATA) & 0xFF);
}


/* =========================================================================
 * --- File Operations 實作區（User Space 與 Kernel Space 的橋梁） ---
 * =========================================================================
 * 當使用者空間呼叫 write() 或 read() 系統呼叫時，核心 VFS 會透過此區函數進行對接。
 */

/**
 * uart_write - 對應 User space 的 write() 動作（例如：echo "leo" > /dev/my_lpuart）
 * @filp: 檔案結構體指標，內含我們在 open 時綁定的 private_data
 * @buf: 來自 User space 的數據緩衝區指標（【注意】核心無法直接解引用此指標，必須使用 get_user）
 * @count: User space 請求寫入的位元組大小
 * @off: 檔案偏移量指標
 */
static ssize_t uart_write(struct file *filp, const char __user *buf, size_t count, loff_t *off)
{
    /* 從 file 結構體中取出我們在 open 階段預先埋好的私有數據指標 */
    struct lpuart_dev *uart = filp->private_data;
    size_t i;
    char c;

    /* 安全檢查：若 User 傳入空指標，回傳 Invalid Argument 錯誤碼 */
    if (!buf) return -EINVAL;

    /* 循序處理 User space 傳入的每一個字元 */
    for (i = 0; i < count; i++) {
        /* get_user(c, buf + i): 
         * 安全地將 1 Byte 數據從使用者空間（buf + i）拷貝到內核空間變數（c）。
         * 內核在此處會檢查記憶體分頁與存取權限，防止 User 用非法指標惡意崩潰內核（Bad Address）。
         */
        if (get_user(c, buf + i)) {
            return -EFAULT; /* 若記憶體拷貝失敗，回傳 Bad Address 錯誤碼 */
        }

        pr_info("[LPUART DEBUG] write: about to send char '%c' to hardware...\n", c);
        
        /* 呼叫前面的底層輪詢發送函式：等待硬體 FIFO 有空間後寫入暫存器 */
        lpuart_putc(uart, c);
    }

    /* 更新檔案偏移量（雖然 UART 是串列流設備、不支援隨機存取 lseek，但維護 *off 是一個好習慣） */
    *off += count;
    
    /* 告訴 User space 系統呼叫：我們已經成功處理並發送了 count 個位元組 */
    return count;
}

/**
 * uart_read - 對應 User space 的 read() 動作（例如：cat /dev/my_lpuart）
 * @buf: User space 的接收緩衝區指標，必須使用 copy_to_user 安全寫入
 * @count: User space 最大期望讀取的位元組數
 */
static ssize_t uart_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
    struct lpuart_dev *uart = filp->private_data;
    size_t bytes_read = 0;
    char c;

    if (!buf || count == 0) return -EINVAL;

    /* 如果 User 想讀取 count 個字元，且尚未滿足需求，就進入循環 */
    while (bytes_read < count) {
        
        /* 呼叫前面的底層輪詢接收函式，此處會阻塞（Block）CPU 直到實體 RX 收到 1 字元 */
        c = lpuart_getc(uart);

        /* copy_to_user(接收端, 來源端, 長度):
         * 安全地將核心變數 &c 的 1 Byte 數據，拷貝回使用者空間的記憶體 buffer。
         * 若回傳值不為 0，代表拷貝過程中斷或記憶體錯誤。
         */
        if (copy_to_user(buf + bytes_read, &c, 1)) {
            return -EFAULT;
        }

        bytes_read++;

        /* 【核心策略】
         * 對於標準的 Polling 讀取實驗，通常採取「收到一個字元就立刻回傳給應用程式」的行為。
         * 這樣應用程式（如終端機監聽）能獲得即時的互動。
         * 如果不加 break，這個 read 函數將會死死卡在 while 迴圈中，直到接滿 count 個字元才會放行。
         */
        break;
    }

    /* 回傳實際成功讀取並拷貝給 User 的位元組數 */
    return bytes_read;
}

/**
 * uart_open - 檔案被開啟時的點火點（例如應用程式執行 open("/dev/my_lpuart", ...)）
 * * 核心生命週期管理：在此函式中實施「按需供電」與「即時初始化波特率」。
 */
static int uart_open(struct inode *inode, struct file *filp)
{
    struct lpuart_dev *uart;
    int ret;
    
    pr_info("[LPUART DEBUG] uart_open called\n");
    
    /* container_of 內核神級巨集：
     * 透過已知成員 cdev 的指標 (inode->i_cdev)，反向推算出包含它的外層大結構體 (struct lpuart_dev) 的實體起點。
     */
    uart = container_of(inode->i_cdev, struct lpuart_dev, cdev);
    
    /* 將推算出來的 uart 結構體指標掛載到 file 的 private_data 欄位中。
     * 如此一來，後續的 uart_read、uart_write 就能跨函式直接共享、撈出這個指標，不需要使用全域變數。
     */
    filp->private_data = uart;

    /* 1. 動態開啟時脈電源：真正將時脈訊號送入 LPUART3 硬體模組（排除 SError 的關鍵） */
    ret = clk_prepare_enable(uart->clk);
    if (ret) {
        pr_err("[LPUART DEBUG] Failed to enable clock\n");
        return ret;
    }
    pr_info("[LPUART DEBUG] Clock enabled in uart_open\n");
    
    /* 2. LPUART 硬體配置初始化流程 */
    
    /* (a) 安全機制：先將 LPUART_CTRL 清零，暫時關閉 TE 與 RE。
     * 依據官方手冊規範，在配置波特率暫存器（BAUD）前，必須確保發送與接收處於關閉狀態。
     */
    writel(0, uart->base + LPUART_CTRL); 

    /* (b) 配置波特率暫存器 (LPUART_BAUD, 偏移量 0x10)
     * 寫入值 0x0F00000D 的含意：
     * - Bit 28-24 (OSR) = 15 -> 設定過採樣率為 16 倍 (15 + 1)
     * - Bit 12-0  (SBR) = 13 -> 設定波特率模數除數為 13
     * 核心計算公式：來源時脈 24MHz / (16 * 13) = 115384 bps (逼近 115200 亂碼終結者)
     */
    writel(0x0F00000D, uart->base + LPUART_BAUD);
    pr_info("[LPUART DEBUG] Baud rate set to 115200 (OSR=16, SBR=13)\n");

    /* (c) 點火啟動：將 CTRL_TE（發送使能）與 CTRL_RE（接收使能）位元寫入控制暫存器。
     * 硬體電路此時正式與外部腳位通訊，驅動程式進入就緒狀態。
     */
    writel((CTRL_TE | CTRL_RE), uart->base + LPUART_CTRL);
    pr_info("[LPUART DEBUG] LPUART_CTRL configured (TE/RE Enabled)\n");
    
    return 0;
}

/* 檔案操作介面結構體：向系統 VFS 註冊，綁定應用程式檔案指標與本驅動核心函式的對應關係 */
static const struct file_operations uart_fops = {
    .owner = THIS_MODULE,
    .open  = uart_open,
    .write = uart_write,
    .read  = uart_read,
    /* 註：本驅動為點對點串列實驗，未實作 release (close) 函式。
     * 實務中，建議可在 release 綁定 uart_close 進行 clk_disable_unprepare(uart->clk) 斷電節能。
     */
};

/**
 * lpuart_probe - 平台設備驅動的初始化進入點
 * @pdev: 指向與此驅動匹配的平台設備結構體（包含從 DTS 解析出來的資源）
 * * 當核心發現 Device Tree 中的 compatible 屬性與驅動匹配時，就會觸發此函式。
 * 其核心任務是：分配私有記憶體、映射硬體暫存器實體位址、獲取時脈資源，並註冊字元設備。
 */
static int lpuart_probe(struct platform_device *pdev)
{
    struct lpuart_dev *uart;
    struct resource *res;
    int ret;

    pr_info("=== [LPUART DEBUG] probe started ===\n");

    /* ---------------------------------------------------------------------
     * 1. 分配驅動私有數據結構體（Device-Managed Memory Allocation）
     * ---------------------------------------------------------------------
     * 使用 devm_kzalloc 確保記憶體生命週期與裝置綁定。
     * 當驅動卸載（remove）或 probe 失敗時，內核會自動釋放此記憶體，避免 Memory Leak。
     */
    uart = devm_kzalloc(&pdev->dev, sizeof(*uart), GFP_KERNEL);
    if (!uart) return -ENOMEM;

    /* ---------------------------------------------------------------------
     * 2. 獲取並映射硬體暫存器實體位址 (I/O Memory Mapping)
     * ---------------------------------------------------------------------
     * platform_get_resource: 從 DTS 節點取得 reg 屬性定義的實體記憶體範圍。
     * devm_ioremap_resource: 執行兩件事：
     * (a) 呼叫 request_mem_region 宣告佔用該段實體位址，防止其他驅動衝突。
     * (b) 將實體位址（i.MX93 的 LPUART3 為 0x42570000）映射到內核虛擬位址空間。
     */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    uart->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(uart->base)) return PTR_ERR(uart->base);
    pr_info("[LPUART DEBUG] Mapped Virtual Base Address: %px\n", uart->base);

    /* ---------------------------------------------------------------------
     * 3. 獲取硬體時脈資源 (Clock Resource Retrieval)
     * ---------------------------------------------------------------------
     * 因為 DTS 沒有定義 clock-names，這裡第二個參數傳入 NULL。
     * 核心會自動退化去抓 clocks 屬性的第 0 個索引（即 IMX93_CLK_LPUART3_GATE）。
     * 【注意】此時僅獲取時脈控制指標（代號），尚未開啟時脈電源，此時嚴禁讀寫暫存器！
     */
    uart->clk = devm_clk_get(&pdev->dev, NULL);
    if (IS_ERR(uart->clk)) {
        ret = PTR_ERR(uart->clk);
        pr_err("[LPUART DEBUG] Failed to get clock by index 0: %d\n", ret);
        return ret;
    }
    pr_info("[LPUART DEBUG] Successfully got clock by index 0\n");

    /* ---------------------------------------------------------------------
     * 4. Linux 字元設備架構註冊 (Character Device Infrastructure)
     * --------------------------------------------------------------------- */
     
    /* 4.1 動態申請主要/次要設備編號 (Major/Minor Device Number)
     * 核心會自動分配一個目前沒人用的主設備號，並建立一個名為 "my_lpuart" 的編號記錄。
     */
    ret = alloc_chrdev_region(&uart->devid, 0, 1, "my_lpuart");
    if (ret < 0) return ret;

    /* 4.2 初始化 cdev 控制結構體，並綁定檔案操作介面 (File Operations)
     * 將虛擬檔案系統（VFS）的 open/close/write 等系統呼叫導向我們寫好的函數。
     * 隨後呼叫 cdev_add 正式將這個字元設備加入核心的核心資料結構中。
     */
    cdev_init(&uart->cdev, &uart_fops);
    uart->cdev.owner = THIS_MODULE;
    ret = cdev_add(&uart->cdev, uart->devid, 1);
    if (ret < 0) goto unregister_chrdev;

    /* 4.3 在 Sysfs 系統中建立類別 (/sys/class/my_lpuart_class)
     * 這是建立自動化設備節點的前提，讓 udev/mdev 機制可以監聽到設備的加入。
     */
    uart->class = class_create("my_lpuart_class");
    if (IS_ERR(uart->class)) {
        ret = PTR_ERR(uart->class);
        goto delete_cdev;
    }

    /* 4.4 真正建立設備節點，會在 /dev/ 底下自動生成 /dev/my_lpuart 節點檔案
     * 這樣 User space 的應用程式（如 echo）就能透過標準檔案路徑來存取這個硬體。
     */
    uart->device = device_create(uart->class, NULL, uart->devid, NULL, "my_lpuart");
    if (IS_ERR(uart->device)) {
        ret = PTR_ERR(uart->device);
        goto destroy_class;
    }

    /* ---------------------------------------------------------------------
     * 5. 保存私有數據指標 (Set Driver Private Data)
     * ---------------------------------------------------------------------
     * 將裝滿資源的 uart 結構體指標塞進平台設備 pdev 的私有資料欄位。
     * 這樣在未來的 lpuart_remove 函數中，才能透過 pdev 重新找回這份寶貴的資料。
     */
    platform_set_drvdata(pdev, uart);
    dev_info(&pdev->dev, "LPUART Driver Probed at %pa\n", &res->start);
    pr_info("=== [LPUART DEBUG] probe completed successfully ===\n");

    return 0;


/* ---------------------------------------------------------------------
 * 錯誤處理路徑 (Error Handling - Unwinding Stack)
 * ---------------------------------------------------------------------
 * 採用經典的 Linux 內核 Goto 標籤設計（潑水節倒退嚕原則）。
 * 如果前面的步驟進行到一半失敗，必須依據「後建立、先拆除」的相反順序，將資源乾淨地釋放。
 */
destroy_class:
    class_destroy(uart->class);
delete_cdev:
    cdev_del(&uart->cdev);
unregister_chrdev:
    unregister_chrdev_region(uart->devid, 1);
    return ret;
}

/**
 * lpuart_remove - 平台設備驅動的卸載/解除綁定進入點
 * @pdev: 指向正在被解除綁定的平台設備
 * * 當 User space 執行 rmmod 或是硬體裝置被熱拔插移除時，核心會呼叫此函式。
 * 任務是徹底清除 probe 階段建立的所有 User space 介面與字元設備。
 */
static void lpuart_remove(struct platform_device *pdev)
{
    /* 透過當初 probe 保存的管道，重新撈出包含虛擬位址、設備號的 uart 結構體 */
    struct lpuart_dev *uart = platform_get_drvdata(pdev);

    /* 1. 銷毀 /dev/my_lpuart 設備節點，並撤銷 /sys/class/ 中的類別 */
    device_destroy(uart->class, uart->devid);
    class_destroy(uart->class);

    /* 2. 從內核中註銷字元設備 (cdev) 的核心管理 */
    cdev_del(&uart->cdev);

    /* 3. 歸還當初動態申請的主要/次要設備編號，讓其他驅動可以重複使用 */
    unregister_chrdev_region(uart->devid, 1);

    dev_info(&pdev->dev, "LPUART Driver Removed\n");
}

/* ---------------------------------------------------------------------
 * 設備匹配與驅動宣告區 (Device Tree Match & Driver Declaration)
 * --------------------------------------------------------------------- */

/* 設備樹匹配表格：核心會比對 DTS 節點中的 compatible 字串是否滿足此陣列 */
static const struct of_device_id lpuart_of_match[] = {
    { .compatible = "hsiao,lpuart" }, /* 必須與 DTS 的 compatible 完全一致 */
    { /* sentinel */ }               /* 哨兵成員：封尾用，內容必須為全空，告知內核陣列結束 */
};

/* 宣告核心平台驅動結構體 */
static struct platform_driver lpuart_driver = {
    .probe = lpuart_probe,           /* 指定硬體匹配成功的進入點 */
    .remove = lpuart_remove,         /* 指定硬體移除或模組卸載的進入點 */
    .driver = {
        .name = "lpuart_driver",     /* 驅動程式名稱（出現在 /sys/bus/platform/drivers/） */
        .of_match_table = lpuart_of_match, /* 綁定上述的設備樹匹配表格 */
    },
};


module_platform_driver(lpuart_driver);
MODULE_LICENSE("GPL");