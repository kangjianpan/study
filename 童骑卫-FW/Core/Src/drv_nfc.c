#include "drv_nfc.h"
#include "drv_bus.h"
#include "app_config.h"
#include <string.h>

static int wr(uint8_t r, uint8_t v)
{ return DrvBus_SpiWrite(1, (uint8_t)((r << 1) & 0x7e), &v, 1); }
static int rd(uint8_t r, uint8_t *v)
{ return DrvBus_SpiRead(1, (uint8_t)(((r << 1) & 0x7e) | 0x80), v, 1); }

static uint16_t crc_a(const uint8_t *p, uint8_t n)
{
    uint16_t crc = 0x6363;
    uint8_t b;
    while (n--)
    {
        b = (uint8_t)(*p++ ^ crc); b ^= (uint8_t)(b << 4);
        crc = (uint16_t)((crc >> 8) ^ ((uint16_t)b << 8) ^ ((uint16_t)b << 3) ^ (b >> 4));
    }
    return crc;
}
static void append_crc(uint8_t *p, uint8_t n)
{
    uint16_t crc = crc_a(p, n);
    p[n] = (uint8_t)crc; p[n + 1] = (uint8_t)(crc >> 8);
}
/* Hardware timer: 5 ms. Also bound the polling loop independently. */
static int exchange(const uint8_t *tx, uint8_t n, uint8_t bits,
                    uint8_t *rx, uint8_t capacity, uint8_t *received)
{
    uint8_t i, irq, error, count, last;
    uint32_t start;
    if (wr(0x01, 0) || wr(0x04, 0x7f) || wr(0x0a, 0x80) || wr(0x0d, bits)) return DRV_ERR;
    for (i = 0; i < n; ++i) if (wr(0x09, tx[i])) return DRV_ERR;
    if (wr(0x01, 0x0c) || wr(0x0d, (uint8_t)(bits | 0x80))) return DRV_ERR;
    start = DrvBus_Now();
    do
    {
        if (rd(0x04, &irq)) return DRV_ERR;
        if (irq & 0x31u) break;
        if ((uint32_t)(DrvBus_Now() - start) >= 8u) return DRV_ERR;
    } while (1);
    if (wr(0x0d, bits) || rd(0x06, &error)) return DRV_ERR;
    if (error & 0x1bu) return DRV_ERR;
    if (irq & 0x01u) return DRV_AGAIN;
    if (rd(0x0a, &count) || rd(0x0c, &last)) return DRV_ERR;
    if (!count || count > capacity || (last & 7u)) return DRV_ERR;
    for (i = 0; i < count; ++i) if (rd(0x09, &rx[i])) return DRV_ERR;
    *received = count;
    return DRV_OK;
}

int DrvNfc_Init(void)
{
    uint8_t version;
    DrvBus_NfcReset(0); DrvBus_Delay(2);
    DrvBus_NfcReset(1); DrvBus_Delay(5);
    if (rd(0x37, &version) || (version != 0x91 && version != 0x92)) return DRV_ERR;
    /* Timer 13.56 MHz / (2*169+1) = 40 kHz; reload 200 = 5 ms. */
    if (wr(0x2a, 0x80) || wr(0x2b, 0xa9) || wr(0x2c, 0) || wr(0x2d, 200) ||
        wr(0x12, 0) || wr(0x13, 0) || wr(0x15, 0x40) || wr(0x11, 0x3d) ||
        wr(0x26, 0x70) || wr(0x14, 0x83)) return DRV_ERR;
    return DRV_OK;
}

int DrvNfc_PollTag(uint8_t uid[10], uint8_t *length)
{
    uint8_t tx[9], rx[10], n, level, used = 0, block[5], sak;
    uint16_t crc;
    int status;
    if (!uid || !length) return DRV_ERR;
    *length = 0;
    tx[0] = 0x52; /* WUPA: also detects the previously halted tag. */
    status = exchange(tx, 1, 7, rx, sizeof(rx), &n);
    if (status != DRV_OK) return status;
    if (n != 2) return DRV_ERR;
    for (level = 0; level < 3; ++level)
    {
        tx[0] = (uint8_t)(0x93 + level * 2); tx[1] = 0x20;
        status = exchange(tx, 2, 0, rx, sizeof(rx), &n);
        if (status != DRV_OK) return status;
        if (n != 5 || (uint8_t)(rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4]) return DRV_ERR;
        memcpy(block, rx, 5);
        tx[1] = 0x70; memcpy(tx + 2, block, 5); append_crc(tx, 7);
        status = exchange(tx, 9, 0, rx, sizeof(rx), &n);
        if (status != DRV_OK) return status;
        crc = crc_a(rx, 1);
        if (n != 3 || rx[1] != (uint8_t)crc || rx[2] != (uint8_t)(crc >> 8)) return DRV_ERR;
        sak = rx[0];
        if (sak & 4u)
        {
            if (level == 2 || block[0] != 0x88) return DRV_ERR;
            memcpy(uid + used, block + 1, 3); used += 3;
        }
        else
        {
            if (block[0] == 0x88) return DRV_ERR;
            memcpy(uid + used, block, 4); used += 4;
            tx[0] = 0x50; tx[1] = 0; append_crc(tx, 2);
            status = exchange(tx, 4, 0, rx, sizeof(rx), &n);
            /* ISO14443-A HALT must time out (silence), never count an error as a tag. */
            if (status != DRV_AGAIN) return DRV_ERR;
            *length = used;
            return DRV_OK;
        }
    }
    return DRV_ERR;
}

int DrvNfc_IsAuthorized(const uint8_t *uid, uint8_t length)
{
    static const uint8_t allowed[10] = APP_HELMET_UID_BYTES;
    return uid && APP_HELMET_UID_LEN != 0 && length == APP_HELMET_UID_LEN &&
           length <= sizeof(allowed) && memcmp(uid, allowed, length) == 0;
}
