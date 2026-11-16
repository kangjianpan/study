#include "drv_tof.h"

/* 待办（B 同学）：实现 I2C1（PB6 SCL / PB7 SDA）上的 VL53L1X
 *   - 两颗上电后默认地址都是 0x29，所以先全部拉低 XSHUT，
 *     再逐个放开、逐个改地址
 *   - 改地址就是往地址寄存器写一次 0x2A
 *   - 量程 40..2000 mm，单次测距的时间预算 20 ms
 *   - I2C 互斥量（g_mtx_i2c）不要跨一整次转换一直攥着：
 *     一次测距 20 ms，攥着锁会让 IMU 那边也等 20 ms */


int DrvTof_Init(void)
{
    return DRV_ERR;
}

int DrvTof_Read(uint16_t dist_mm[2])
{
    if (dist_mm != 0)
    {
        dist_mm[0] = 4000;      /* 报一个很远的值 = 前方没有障碍 */
        dist_mm[1] = 4000;
    }
    return DRV_ERR;
}
