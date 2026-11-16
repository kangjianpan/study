#ifndef __DRV_IMU_H
#define __DRV_IMU_H

#include <stdint.h>

/* ICM-42688-P on SPI1, CS PA4. DRV_AGAIN does not refresh sample age. */

#define DRV_OK   0
#define DRV_ERR (-1)
#define DRV_AGAIN 1

int DrvImu_Init(void);
int DrvImu_Read(int16_t accel_mg[3], int16_t gyro_dps10[3]);

#endif /* __DRV_IMU_H */
