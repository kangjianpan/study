#ifndef __APP_STATE_H
#define __APP_STATE_H

#include <stdint.h>

/* 系统状态机（对应方案文档 7.5 节）
 *
 * 之所以要一个显式状态机、而不是"一出故障就断电"，关键就在 DECEL 这个状态：
 * 碰上可恢复的故障（佩戴连锁失效、超速）时，先把油门慢慢收下来；
 * 只有降速之后故障还在，才切断驱动。
 * 因为高速行驶中突然断电，本身就是危险动作。 */

typedef enum
{
    APP_STATE_SELF_TEST = 0,
    APP_STATE_STANDBY,
    APP_STATE_READY,
    APP_STATE_RUN,
    APP_STATE_DECEL,
    APP_STATE_FAULT_LOCK,
    APP_STATE_COUNT
} app_state_t;

/* 由 SafetyTask 写，ControlTask 和人机界面只读。 */
extern volatile app_state_t g_app_state;

const char *App_State_Name(app_state_t st);

/* READY、RUN、DECEL 三个状态都允许驱动输出。
 * DECEL 也得让电调活着，否则没东西可收油门。 */
uint8_t App_State_MotorEnabled(app_state_t st);

#endif /* __APP_STATE_H */
