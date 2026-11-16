#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include "app_types.h"
#include "app_state.h"
int g_q_sensor, g_eg_fault;
volatile uint32_t g_control_tick, g_local_faults, g_last_link_tick;
volatile uint8_t g_drive_allowed,g_arm_can_run,g_sensor_initializing,g_dbg_interlock_fail,g_link_seen;
volatile int16_t g_dbg_throttle = -1;
static uint32_t tick;
static uint8_t estop = 1, feedback, enabled;
static int step, limit, fed;
static jmp_buf stop;
static sensor_data_t sample;
void SafetyTask(void *arg);
uint32_t xTaskGetTickCount(void) { return tick; }
uint32_t HAL_GetTick(void) { return tick; }
int HAL_GPIO_ReadPin(int p,int n) { (void)p; return n == 1 ? estop : feedback; }
void HAL_GPIO_WritePin(int p,int n,int v) { (void)p; (void)n; (void)v; }
void vTaskDelayUntil(uint32_t *last,uint32_t period)
{
    if (++step > limit) longjmp(stop, 1);
    *last += period; tick = *last; g_control_tick = tick; sample.sample_tick = tick;
#ifdef TEST_COMMISSIONED
    /* Press ARM with the pedal down, wait, release, then press again. */
    if (tick < 3500) assert(g_app_state != APP_STATE_RUN);
    if (tick == 3510) assert(g_app_state == APP_STATE_RUN && g_drive_allowed);
    if (tick == 3610) assert(g_app_state == APP_STATE_STANDBY && !g_drive_allowed);
    if (tick == 3710) assert(g_app_state == APP_STATE_FAULT_LOCK && !g_drive_allowed);
    feedback = tick >= 250 && tick < 3600;
    sample.throttle_permille = tick >= 250 && tick < 3400 ? 500 : 0;
    if (tick >= 3500) sample.throttle_permille = 500;
    if (tick >= 3700) sample.tof_present = 0;
#endif
}
int xQueuePeek(int q,void *out,int t) { (void)q; (void)t; memcpy(out,&sample,sizeof(sample)); return 1; }
uint32_t xEventGroupGetBits(int e) { (void)e; return 0; }
void App_GetLimitSnapshot(limit_cfg_t *out) { limit_cfg_t l={12,10,12,0,10}; *out=l; }
void App_SetLinkFallback(uint8_t v) { (void)v; }
void App_LogEvent(uint8_t c,uint16_t v) { (void)c; (void)v; }
void Bsp_Iwdg_Feed(void) { ++fed; }
int16_t Motor_GetOutput(void) { return 0; }
void Motor_Enable(uint8_t v)
{
    enabled=v;
#ifndef TEST_COMMISSIONED
    assert(!v);
#endif
}
void Motor_Stop(void) {}
int main(void)
{
    sample.sample_valid=1; sample.imu_present=sample.tof_present=sample.nfc_present=1;
    sample.speed_valid=1; sample.seat_pressed=sample.belt_closed=sample.helmet_ok=1;
    sample.bus_mv=25900; sample.accel_mg[2]=1000;
    limit=25;
#ifdef TEST_COMMISSIONED
    limit=375;
    if (!setjmp(stop)) SafetyTask(0);
    assert(g_app_state==APP_STATE_FAULT_LOCK && !enabled);
    assert(g_local_faults & FAULT_SENSOR);
    puts("Actual SafetyTask: ARM, dwell, fresh pedal, lost feedback and sensor fault passed");
    return 0;
#endif
    if (!setjmp(stop)) SafetyTask(0);
    assert(g_app_state==APP_STATE_FAULT_LOCK && !enabled && fed==25);
    assert(g_local_faults==FAULT_COMMISSION);
    feedback=1; step=0; limit=5;
    if (!setjmp(stop)) SafetyTask(0);
    assert(g_local_faults & FAULT_ARM);
    assert(!g_drive_allowed);
    puts("Actual SafetyTask: drive lock and ARM stuck-high fault passed");
    return 0;
}
