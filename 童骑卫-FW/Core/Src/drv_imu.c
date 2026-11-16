#include "drv_imu.h"

/* 待办（B 同学）：实现 SPI1 上的 ICM-42688-P
 *   - SPI1：PA5 SCK / PA6 MISO / PA7 MOSI / PA4 CS
 *   - WHO_AM_I（0x75 寄存器）必须读到 0x47
 *   - 量程：加速度 ±4 g，角速度 ±500 dps
 *   - 换算：accel_mg   = raw * 4000 / 32768（约等于毫 g）
 *           gyro_dps10 = raw * 5000 / 32768（0.1 度/秒）
 *
 * 在这之前桩函数一律返回"没有硬件"，
 * 这样 SensorTask 会停在台架模式，而不是误报一个侧翻故障把车锁死。
 * （驱动接进来后，访问 SPI1 前同样要先拿 g_mtx_i2c。） */

int DrvImu_Init(void)
{
    return DRV_ERR;
}

int DrvImu_Read(int16_t accel_mg[3], int16_t gyro_dps10[3])
{
    if (accel_mg != 0)
    {
        accel_mg[0] = 0;
        accel_mg[1] = 0;
        accel_mg[2] = 1000;     /* 假装车身水平：1 g 压在 Z 轴上 */
    }
    if (gyro_dps10 != 0)
    {
        gyro_dps10[0] = 0;
        gyro_dps10[1] = 0;
        gyro_dps10[2] = 0;
    }
    return DRV_ERR;
}
