#pragma once

#include <stdint.h>
#include <stddef.h>

/*  AA 55 | SEQ | CMD | LEN | PAYLOAD | CRC8
 *  CRC8: poly 0x07, init 0x00, covers SEQ..PAYLOAD (not the header).
 *  Byte-for-byte compatible with protocol.c on the STM32.  */

uint8_t Frame_Crc8(uint8_t crc, uint8_t byte);

/*  Returns the frame length, or 0 if the payload does not fit.  */
size_t Frame_Build(uint8_t *out, size_t cap, uint8_t seq, uint8_t cmd,
                   const uint8_t *payload, uint8_t len);
