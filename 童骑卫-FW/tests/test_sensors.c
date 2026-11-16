#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "drv_bus.h"
#include "drv_imu.h"
#include "drv_tof.h"
#include "drv_nfc.h"

static uint32_t now;
static int fail_bus;
static uint8_t imu[128];
static uint8_t tof[2][512], active[2], addr[2];
static int tof_init;
static uint8_t nfc_regs[64], fifo[32], tx[32], tn, rn, ri, stage, corrupt;

uint32_t DrvBus_Now(void) { return now++; }
void DrvBus_Delay(uint32_t ms) { now += ms; }
void DrvBus_TofShutdown(uint8_t i, uint8_t on)
{
    active[i] = on;
    if (on)
    {
        addr[i] = 0x29; memset(tof[i], 0, sizeof(tof[i]));
        tof[i][0xe5] = 1; tof[i][0x10f] = 0xea; tof[i][0x110] = 0xcc;
        tof[i][0xde] = 1; tof[i][0xdf] = 0x20;
    }
}
void DrvBus_NfcReset(uint8_t on) { (void)on; }
static uint16_t crc(const uint8_t *p, uint8_t n)
{
    uint16_t c = 0x6363; uint8_t b;
    while (n--) { c ^= *p++; for (b = 0; b < 8; ++b) c = (c & 1) ? (c >> 1) ^ 0x8408 : c >> 1; }
    return c;
}
static void response(void)
{
    uint16_t c;
    ri = 0; rn = 0; nfc_regs[0x04] = 0x30;
    switch (stage++)
    {
    case 0: assert(tn == 1 && tx[0] == 0x52); fifo[0] = 0x44; fifo[1] = 0; rn = 2; break;
    case 1:
        assert(tn == 2 && tx[0] == 0x93 && tx[1] == 0x20);
        fifo[0] = 0x88; fifo[1] = 1; fifo[2] = 2; fifo[3] = 3;
        fifo[4] = 0x88 ^ 1 ^ 2 ^ 3 ^ (corrupt == 1); rn = 5; break;
    case 2: case 4:
        assert(tn == 9 && tx[1] == 0x70);
        c = crc(tx, 7); assert(tx[7] == (uint8_t)c && tx[8] == (uint8_t)(c >> 8));
        fifo[0] = stage == 3 ? 4 : 0; c = crc(fifo, 1);
        fifo[1] = (uint8_t)c; fifo[2] = (uint8_t)((c >> 8) ^ (corrupt == 2)); rn = 3; break;
    case 3:
        assert(tn == 2 && tx[0] == 0x95 && tx[1] == 0x20);
        fifo[0] = 4; fifo[1] = 5; fifo[2] = 6; fifo[3] = 7; fifo[4] = 4 ^ 5 ^ 6 ^ 7; rn = 5; break;
    case 5: assert(tx[0] == 0x50); nfc_regs[0x04] = 1; break;
    default: nfc_regs[0x04] = 1; break;
    }
}
int DrvBus_SpiRead(uint8_t dev, uint8_t command, uint8_t *data, uint16_t n)
{
    uint8_t reg;
    if (fail_bus) return DRV_ERR;
    if (!dev) { memcpy(data, imu + (command & 0x7f), n); return DRV_OK; }
    assert(n == 1); reg = (command & 0x7e) >> 1;
    if (reg == 0x09) { assert(ri < rn); *data = fifo[ri++]; }
    else if (reg == 0x0a) *data = rn;
    else *data = nfc_regs[reg];
    return DRV_OK;
}
int DrvBus_SpiWrite(uint8_t dev, uint8_t command, const uint8_t *data, uint16_t n)
{
    uint8_t reg;
    if (fail_bus) return DRV_ERR;
    if (!dev) { memcpy(imu + command, data, n); return DRV_OK; }
    assert(n == 1); reg = (command & 0x7e) >> 1;
    if (reg == 0x09) tx[tn++] = *data;
    else if (reg == 0x0a && (*data & 0x80)) tn = 0;
    else if (reg == 0x04) nfc_regs[reg] = 0;
    else nfc_regs[reg] = *data;
    if (reg == 0x0d && (*data & 0x80)) response();
    return DRV_OK;
}
static int index_of(uint8_t a)
{
    int i, found = -1;
    for (i = 0; i < 2; ++i) if (active[i] && addr[i] == a) { assert(found == -1); found = i; }
    return found;
}
int DrvBus_I2cRead(uint8_t a, uint16_t r, uint8_t *p, uint16_t n)
{
    int i = index_of(a);
    if (fail_bus || i < 0) return DRV_ERR;
    assert(r + n <= 512); memcpy(p, tof[i] + r, n);
    if (tof_init && r == 0x31) *p = 1;
    return DRV_OK;
}
int DrvBus_I2cWrite(uint8_t a, uint16_t r, const uint8_t *p, uint16_t n)
{
    int i = index_of(a);
    if (fail_bus || i < 0) return DRV_ERR;
    assert(r + n <= 512); memcpy(tof[i] + r, p, n);
    if (r == 1) addr[i] = *p;
    return DRV_OK;
}
int main(void)
{
    int16_t acc[3], gyro[3]; uint16_t d[2]; uint8_t uid[10], len;
    uint8_t expected[7] = {1,2,3,4,5,6,7};
    imu[0x75] = 0x47;
    assert(DrvImu_Init() == DRV_OK && imu[0x4f] == 0x48 && imu[0x50] == 0x48);
    assert(DrvImu_Read(acc, gyro) == DRV_AGAIN);
    imu[0x2d] = 8; imu[0x1f] = 0x20; imu[0x21] = 0xe0; imu[0x23] = 0x10; imu[0x25] = 0x40;
    assert(DrvImu_Read(acc, gyro) == DRV_OK && acc[0] == 1000 && acc[1] == -1000 && acc[2] == 500 && gyro[0] == 2500);
    imu[0x1f] = 0x80; assert(DrvImu_Read(acc, gyro) == DRV_ERR);
    imu[0x75] = 0; assert(DrvImu_Init() == DRV_ERR);
    tof_init = 1; assert(DrvTof_Init() == DRV_OK); tof_init = 0;
    assert(addr[0] == 0x2a && addr[1] == 0x2b);
    assert(tof[0][0x5e] == 0 && tof[0][0x5f] == 0x1e && tof[1][0x62] == 0x22);
    assert(DrvTof_Read(d) == DRV_AGAIN);
    tof[0][0x31] = 1; tof[1][0x31] = 1;
    tof[0][0x89] = tof[1][0x89] = 9;
    tof[0][0x96] = 1; tof[0][0x97] = 244; tof[1][0x96] = 3; tof[1][0x97] = 232;
    assert(DrvTof_Read(d) == DRV_OK && d[0] == 500 && d[1] == 1000);
    tof[0][0x31] = tof[1][0x31] = 0; now += 101;
    assert(DrvTof_Read(d) == DRV_ERR);
    tof[0][0x31] = 1; tof[0][0x89] = 4;
    assert(DrvTof_Read(d) == DRV_ERR);
    fail_bus = 1; assert(DrvTof_Read(d) == DRV_ERR); fail_bus = 0;
    nfc_regs[0x37] = 0x92; assert(DrvNfc_Init() == DRV_OK);
    assert(DrvNfc_PollTag(uid, &len) == DRV_OK && len == 7 && memcmp(uid, expected, 7) == 0);
    assert(DrvNfc_IsAuthorized(uid, len));
    uid[6] = 8; assert(!DrvNfc_IsAuthorized(uid, len));
    assert(!DrvNfc_IsAuthorized(expected, 4));
    assert(DrvNfc_PollTag(uid, &len) == DRV_AGAIN && len == 0);
    stage = 0; corrupt = 1; assert(DrvNfc_PollTag(uid, &len) == DRV_ERR);
    stage = 0; corrupt = 2; assert(DrvNfc_PollTag(uid, &len) == DRV_ERR);
    fail_bus = 1; assert(DrvNfc_PollTag(uid, &len) == DRV_ERR);
    puts("IMU, dual ToF, NFC full UID and faults: passed");
    return 0;
}
