/* VL53L1X configuration/init derived from STMicroelectronics (c) 2018.
 * BSD-3-Clause notice: see THIRD_PARTY_NOTICES.md. All waits are bounded. */
#include "drv_tof.h"
#include "drv_bus.h"
#include "app_config.h"

static const uint8_t s_default[91] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08, 0x00, 0x08, 0x10, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
    0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xc8, 0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00, 0x00, 0x01, 0xcc, 0x0f,
    0x01, 0xf1, 0x0d, 0x01, 0x68, 0x00, 0x80, 0x08, 0xb8, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0f, 0x0d, 0x0e,
    0x0e, 0x00, 0x00, 0x02, 0xc7, 0xff, 0x9B, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
};

static const uint8_t s_address[2] = {0x2a, 0x2b};
static uint16_t s_distance[2];
static uint32_t s_updated[2];
static uint8_t s_have[2], s_running;

static int wr(uint8_t a, uint16_t r, uint8_t v)
{ return DrvBus_I2cWrite(a, r, &v, 1); }
static int word(uint8_t a, uint16_t r, uint16_t v)
{
    uint8_t d[2]; d[0] = (uint8_t)(v >> 8); d[1] = (uint8_t)v;
    return DrvBus_I2cWrite(a, r, d, 2);
}
static int ready(uint8_t a, uint8_t *result)
{
    uint8_t mux, status;
    if (DrvBus_I2cRead(a, 0x30, &mux, 1) || DrvBus_I2cRead(a, 0x31, &status, 1))
        return DRV_ERR;
    *result = ((status & 1u) == ((mux & 0x10u) ? 0u : 1u));
    return DRV_OK;
}
static int wait_ready(uint8_t a)
{
    uint32_t start = DrvBus_Now();
    uint8_t r;
    do
    {
        if (ready(a, &r)) return DRV_ERR;
        if (r) return DRV_OK;
        DrvBus_Delay(2);
    } while ((uint32_t)(DrvBus_Now() - start) < 150u);
    return DRV_ERR;
}
static int init_one(uint8_t i)
{
    uint8_t a = s_address[i], boot, d[4];
    uint16_t pll;
    uint32_t start, period;
    DrvBus_TofShutdown(i, 1);
    DrvBus_Delay(3);
    start = DrvBus_Now();
    do
    {
        if (DrvBus_I2cRead(0x29, 0xe5, &boot, 1)) return DRV_ERR;
        if (boot & 1u) break;
        if ((uint32_t)(DrvBus_Now() - start) > 100u) return DRV_ERR;
        DrvBus_Delay(2);
    } while (1);
    if (DrvBus_I2cRead(0x29, 0x10f, d, 2) || d[0] != 0xea || d[1] != 0xcc ||
        wr(0x29, 0x0001, a) || DrvBus_I2cWrite(a, 0x2d, s_default, sizeof(s_default)))
        return DRV_ERR;
    /* AVDD-level I2C/GPIO, initial VHV calibration, then long mode 20 ms. */
    if (wr(a, 0x2e, 1) || wr(a, 0x2f, 1) || wr(a, 0x87, 0x40) || wait_ready(a) ||
        wr(a, 0x86, 1) || wr(a, 0x87, 0) || wr(a, 0x08, 9) || wr(a, 0x0b, 0) ||
        wr(a, 0x4b, 0x0a) || wr(a, 0x60, 0x0f) || wr(a, 0x63, 0x0d) || wr(a, 0x69, 0xb8) ||
        word(a, 0x78, 0x0f0d) || word(a, 0x7a, 0x0e0e) ||
        word(a, 0x5e, 0x001e) || word(a, 0x61, 0x0022) ||
        DrvBus_I2cRead(a, 0xde, d, 2)) return DRV_ERR;
    pll = (uint16_t)((((uint16_t)d[0] << 8) | d[1]) & 0x3ffu);
    if (!pll) return DRV_ERR;
    period = (uint32_t)pll * TOF_INTERMEASUREMENT_MS * 1075u / 1000u;
    d[0] = (uint8_t)(period >> 24); d[1] = (uint8_t)(period >> 16);
    d[2] = (uint8_t)(period >> 8); d[3] = (uint8_t)period;
    if (DrvBus_I2cWrite(a, 0x6c, d, 4) || wr(a, 0x86, 1) || wr(a, 0x87, 0x40))
        return DRV_ERR;
    return DRV_OK;
}

int DrvTof_Init(void)
{
    uint8_t i;
    s_running = 0;
    DrvBus_TofShutdown(0, 0); DrvBus_TofShutdown(1, 0);
    DrvBus_Delay(2);
    for (i = 0; i < 2; ++i)
    {
        s_have[i] = 0;
        if (init_one(i))
        {
            DrvBus_TofShutdown(0, 0); DrvBus_TofShutdown(1, 0);
            return DRV_ERR;
        }
    }
    s_running = 1;
    return DRV_OK;
}

int DrvTof_Read(uint16_t dist_mm[2])
{
    uint8_t i, r, data[17];
    uint16_t distance;
    uint8_t both = 1;
    if (!s_running || !dist_mm) return DRV_ERR;
    for (i = 0; i < 2; ++i)
    {
        if (ready(s_address[i], &r)) return DRV_ERR;
        if (r)
        {
            if (DrvBus_I2cRead(s_address[i], 0x89, data, sizeof(data)) || wr(s_address[i], 0x86, 1))
                return DRV_ERR;
            distance = (uint16_t)(((uint16_t)data[13] << 8) | data[14]);
            /* Only ULD range-status 0 (raw 9) is trusted. No target is NOT a fabricated clear path. */
            if ((data[0] & 0x1fu) != 9 || !distance || distance > 4000u) return DRV_ERR;
            s_distance[i] = distance; s_updated[i] = DrvBus_Now(); s_have[i] = 1;
        }
        if (!s_have[i]) both = 0;
        else if ((uint32_t)(DrvBus_Now() - s_updated[i]) > TOF_STALE_MS) return DRV_ERR;
    }
    if (!both) return DRV_AGAIN;
    dist_mm[0] = s_distance[0]; dist_mm[1] = s_distance[1];
    return DRV_OK;
}
