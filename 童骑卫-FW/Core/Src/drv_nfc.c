#include "drv_nfc.h"

/* 待办（B 同学）：实现 SPI1 上的 MFRC522
 *   - SPI1（PA5 SCK / PA6 MISO / PA7 MOSI）和 IMU 共用；
 *     本芯片用 PA2 做片选，IMU 用 PA4。
 *     两个驱动在动总线之前都必须先拿到同一把 SPI 互斥量 ——
 *     就是 g_mtx_i2c，创建在 app_start() 里，见 app_main.h 的说明。
 *   - 只有头盔白名单里的 UID 才算有效。 */

int DrvNfc_Init(void)
{
    return DRV_ERR;
}

int DrvNfc_PollTag(uint8_t uid[4])
{
    if (uid != 0)
    {
        uid[0] = 0;
        uid[1] = 0;
        uid[2] = 0;
        uid[3] = 0;
    }
    return DRV_ERR;
}
