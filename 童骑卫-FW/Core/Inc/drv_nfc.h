#ifndef __DRV_NFC_H
#define __DRV_NFC_H

#include <stdint.h>
#include "drv_imu.h"

/* MFRC522 读卡芯片，走 SPI1；头盔上贴的标签是一张 NTAG213。 */

int DrvNfc_Init(void);

/* Full ISO14443-A UID (4/7/10 bytes). AGAIN=no tag, ERR=reader/protocol error.
 * A detected UID is not authorization; callers must check the full allowlist. */
int DrvNfc_PollTag(uint8_t uid[10], uint8_t *length);
int DrvNfc_IsAuthorized(const uint8_t *uid, uint8_t length);

#endif /* __DRV_NFC_H */
