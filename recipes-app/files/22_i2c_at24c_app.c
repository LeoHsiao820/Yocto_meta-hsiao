#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "fcntl.h"
#include "unistd.h"

/**
 * AT24C256 EEPROM 測試程式
 * * 用法: ./at24_test /dev/at24c256
 * * 此程式展示了典型的 Linux 檔案操作流程：
 * 1. open()  -> 呼叫驅動的 at24_open
 * 2. write() -> 呼叫驅動的 at24_eeprom_write (處理分頁與 16-bit 地址)
 * 3. lseek() -> 呼叫驅動的 at24_eeprom_llseek (重設讀取指標)
 * 4. read()  -> 呼叫驅動的 at24_eeprom_read
 */

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    char *filename;
    /* 測試寫入的字串 */
    char write_data[] = "i.MX93_AT24C256_TEST";
    char read_buf[64] = {0};

    printf("--- AT24C256 eeprom test ---\n");

    /* 檢查命令列參數，確保使用者有輸入裝置路徑 */
    if (argc != 2) {
        printf("Usage: %s <device_path>\n", argv[0]);
        return 1;
    }

    filename = argv[1];
    
    /* * 0. 打開裝置檔案
     * O_RDWR: 以可讀可寫模式開啟。
     * 成功後會回傳檔案描述符 (File Descriptor, fd)。
     */
    fd = open(filename, O_RDWR);
    if (fd < 0) {
        printf("Error: Cannot open %s. Check permissions or if driver is loaded.\n", filename);
        return 1;
    }

    /* * 1. 寫入測試
     * 驅動程式內部已經封裝了 16-bit 地址處理與 Page Write 邏輯，
     * 所以應用層只需像寫入一般檔案一樣傳送純資料。
     */
    printf("Writing data: %s\n", write_data);
    if (write(fd, write_data, strlen(write_data)) < 0) {
        perror("Write failed");
    }

    /* * 重要提示：EEPROM 硬體完成物理燒錄需要時間 (Write Cycle Time)。
     * 雖然驅動內部已有延遲，但在應用層多加一點緩衝可確保讀取前資料已固化。
     */
    usleep(10000); 

    /* * 2. 讀取測試
     * 在 read() 之前，必須先將讀取緩衝區清空。
     */
    memset(read_buf, 0, sizeof(read_buf));

    /* * 定位指標 (lseek)
     * 因為之前的 write() 操作已經讓檔案指標 (f_pos) 移到了資料末端，
     * 若要從頭讀取，必須使用 lseek 將指標指回 0 (SEEK_SET)。
     */
    lseek(fd, 0, SEEK_SET);

    printf("Reading data...\n");

    /* 從 EEPROM 讀取資料至 read_buf */
    ret = read(fd, read_buf, sizeof(read_buf));

    if (ret < 0) {
        perror("Read failed");
        close(fd);
        return -1;
    } 

    /* * 資料清理邏輯：
     * 1. 加上字串終結符 \0。
     * 2. EEPROM 出廠或被抹除後通常值為 0xFF，若遇到 0xFF 表示到達資料邊界。
     */
    read_buf[ret] = '\0'; 
    for(int i = 0; i < ret; i++) {
        if((unsigned char)read_buf[i] == 0xFF) {
            read_buf[i] = '\0';
            break;
        }
    }
    
    printf("Read data back: [%s]\n", read_buf);

    /* * 3. 驗證結果
     * 比較寫入與讀回的字串是否完全一致。
     */
    if (strncmp(write_data, read_buf, strlen(write_data)) == 0) {
        printf(">> Validation passed! Data integrity confirmed.\n");
    } else {
        printf(">> Validation failed!\n");
        printf("   Expected: [%s]\n", write_data);
        printf("   Received: [%s]\n", read_buf);
        printf(">> Please check I2C driver logic (Address bytes, Page Write boundaries).\n");
    }
    
    /* 釋放資源 */
    close(fd);

    return 0;
}