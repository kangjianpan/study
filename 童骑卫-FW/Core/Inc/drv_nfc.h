#ifndef __DRV_NFC_H
#define __DRV_NFC_H

#include <stdint.h>
#include "drv_imu.h"

/* MFRC522 读卡芯片，走 SPI1；头盔上贴的标签是一张 NTAG213。 */

int DrvNfc_Init(void);

/* 读到有效头盔标签时返回 DRV_OK，并把 4 字节 UID 填进 uid。 */
int DrvNfc_PollTag(uint8_t uid[4]);

#endif /* __DRV_NFC_H */
