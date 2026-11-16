#ifndef __BSP_H
#define __BSP_H

#include <stdint.h>
#include "main.h"

/* 板级支持。所有直接操作寄存器的代码都收在这里，
 * 这样上面的应用层任务就不用关心具体用的是哪个外设。 */

void     Bsp_Gpio_Init(void);
void     Bsp_Usart3_Init(void);        /* 和 ESP32 通信，PB10/PB11，115200 */
void     Bsp_Adc_Init(void);
uint16_t Bsp_Adc_Read(uint32_t channel);

void     Bsp_Iwdg_Init(void);
void     Bsp_Iwdg_Feed(void);

#define RESET_UNKNOWN   0u
#define RESET_PIN       1u
#define RESET_IWDG      2u
uint8_t  Bsp_GetResetReason(void);
void     Bsp_ClearResetFlags(void);

extern UART_HandleTypeDef huart3;
extern ADC_HandleTypeDef  hadc1;

#endif /* __BSP_H */
