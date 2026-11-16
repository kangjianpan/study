#ifndef __DRV_TOF_H
#define __DRV_TOF_H

#include <stdint.h>
#include "drv_imu.h"

/* 两个 VL53L1X 激光测距挂在 I2C1 上。两颗默认地址相同，
 * XSHUT: PB3 / PB9; 7-bit addresses: left 0x2a, right 0x2b. */

int DrvTof_Init(void);
int DrvTof_Read(uint16_t dist_mm[2]);

#endif /* __DRV_TOF_H */
