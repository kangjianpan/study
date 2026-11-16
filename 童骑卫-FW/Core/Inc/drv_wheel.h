#ifndef DRV_WHEEL_H
#define DRV_WHEEL_H
#include <stdint.h>
void DrvWheel_Init(void);
void DrvWheel_Edge(uint8_t index, uint32_t now_ms);
int DrvWheel_Read(uint16_t *speed_x100, uint16_t *pulses);
#endif
