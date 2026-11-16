#include "drv_imu.h"
#include "drv_bus.h"

static int write_reg(uint8_t reg, uint8_t value)
{ return DrvBus_SpiWrite(0, reg & 0x7fu, &value, 1); }
static int read_reg(uint8_t reg, uint8_t *value, uint16_t n)
{ return DrvBus_SpiRead(0, reg | 0x80u, value, n); }

int DrvImu_Init(void)
{
    uint8_t id;
    if (write_reg(0x76, 0) || write_reg(0x11, 1)) return DRV_ERR;
    DrvBus_Delay(2);
    if (read_reg(0x75, &id, 1) || id != 0x47) return DRV_ERR;
    /* Bank 0: +/-500 dps, +/-4 g, 100 Hz, low-noise mode. */
    if (write_reg(0x4f, 0x48) || write_reg(0x50, 0x48) || write_reg(0x4e, 0x0f))
        return DRV_ERR;
    DrvBus_Delay(50);
    if (read_reg(0x4f, &id, 1) || id != 0x48 ||
        read_reg(0x50, &id, 1) || id != 0x48) return DRV_ERR;
    return DRV_OK;
}

int DrvImu_Read(int16_t accel_mg[3], int16_t gyro_dps10[3])
{
    uint8_t status, data[12], i;
    int16_t raw;
    if (!accel_mg || !gyro_dps10 || read_reg(0x2d, &status, 1)) return DRV_ERR;
    if (!(status & 0x08u)) return DRV_AGAIN;
    if (read_reg(0x1f, data, sizeof(data))) return DRV_ERR;
    for (i = 0; i < 6; ++i)
    {
        raw = (int16_t)(((uint16_t)data[i * 2] << 8) | data[i * 2 + 1]);
        if (raw == (int16_t)-32768) return DRV_ERR;
        if (i < 3) accel_mg[i] = (int16_t)((int32_t)raw * 4000 / 32768);
        else gyro_dps10[i - 3] = (int16_t)((int32_t)raw * 5000 / 32768);
    }
    return DRV_OK;
}
