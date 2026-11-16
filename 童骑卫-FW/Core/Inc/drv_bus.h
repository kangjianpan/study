#ifndef DRV_BUS_H
#define DRV_BUS_H
#include <stdint.h>
/* SPI selects: 0 = ICM-42688-P PA4, 1 = MFRC522 PA2.
 * I2C helpers accept unshifted 7-bit addresses. */
int DrvBus_Init(void);
int DrvBus_SpiRead(uint8_t device, uint8_t command, uint8_t *data, uint16_t length);
int DrvBus_SpiWrite(uint8_t device, uint8_t command, const uint8_t *data, uint16_t length);
int DrvBus_I2cRead(uint8_t address, uint16_t reg, uint8_t *data, uint16_t length);
int DrvBus_I2cWrite(uint8_t address, uint16_t reg, const uint8_t *data, uint16_t length);
uint32_t DrvBus_Now(void);
void DrvBus_Delay(uint32_t ms);
void DrvBus_TofShutdown(uint8_t index, uint8_t release);
void DrvBus_NfcReset(uint8_t release);
#endif
