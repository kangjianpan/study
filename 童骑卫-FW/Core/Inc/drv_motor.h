#ifndef __DRV_MOTOR_H
#define __DRV_MOTOR_H

#include <stdint.h>

/* 电调接口：50 Hz PWM，脉宽 1000-2000 us，1500 us 为中立位 */

void    Motor_Init(void);
void    Motor_Enable(uint8_t on);
void    Motor_Stop(void);

/* 输出量：-1000（全速倒车）.. 0（中立）.. +1000（全速前进） */
void    Motor_SetOutput(int16_t permille);
int16_t Motor_GetOutput(void);

#endif /* __DRV_MOTOR_H */
