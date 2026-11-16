#ifndef __APP_PINS_H
#define __APP_PINS_H

#include "main.h"

/* GuardKart 引脚表（STM32F103C8T6，兼容蓝色最小系统板）
 *
 * 调试串口  PA9 / PA10   USART1  115200   （CubeMX 已经配好）
 * ESP32     PB10 / PB11  USART3  115200
 * 电调 PWM  PA8          TIM1_CH1 50 Hz，脉宽 1000-2000 us
 * 心跳      PB13         每 10 ms（SafetyTask 一拍）翻转一次
 * 电机使能  PB12         高电平 = 允许硬件手动使能
 * 急停      PB15         低电平有效，底板常闭回路断线下拉
 * 安全带    PB14         霍尔开关，扣合时为低
 * 蜂鸣器    PA15         高电平响
 * 状态灯    PC13         低电平点亮（板载 LED1）
 * ADC       PA0 电流 / PA1 母线电压 / PA3 踏板 / PB0 座垫压力片
 *--------------------------------------------------------------------------*/

#define MOTOR_EN_Pin            GPIO_PIN_12
#define MOTOR_EN_GPIO_Port      GPIOB

#define HB_Pin                  GPIO_PIN_13
#define HB_GPIO_Port            GPIOB

#define ESTOP_Pin               GPIO_PIN_15
#define ESTOP_GPIO_Port         GPIOB

#define BELT_Pin                GPIO_PIN_14
#define BELT_GPIO_Port          GPIOB

#define BUZZER_Pin              GPIO_PIN_15
#define BUZZER_GPIO_Port        GPIOA

#define STATUS_LED_Pin          GPIO_PIN_13
#define STATUS_LED_GPIO_Port    GPIOC

/* ADC 通道分配 */
#define ADC_CH_CURRENT          ADC_CHANNEL_0     /* PA0 电流采样 */
#define ADC_CH_VBUS             ADC_CHANNEL_1     /* PA1 母线电压 */
#define ADC_CH_PEDAL            ADC_CHANNEL_3     /* PA3 踏板 */
#define ADC_CH_SEAT             ADC_CHANNEL_8     /* PB0 座垫压力片 */

/* PC13 is arm feedback INPUT; the on-core LED must not be driven by HMI. */
#define ARM_OK_Pin              GPIO_PIN_13
#define ARM_OK_GPIO_Port        GPIOC
#define TOF_XSHUT1_Pin          GPIO_PIN_3
#define TOF_XSHUT1_GPIO_Port     GPIOB
#define TOF_XSHUT2_Pin          GPIO_PIN_9
#define TOF_XSHUT2_GPIO_Port     GPIOB
#define NFC_RST_Pin             GPIO_PIN_8
#define NFC_RST_GPIO_Port       GPIOB
#define ADC_CH_SEAT_R           ADC_CHANNEL_9 /* PB1 */

#endif /* __APP_PINS_H */
