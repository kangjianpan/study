#include "app_main.h"
#include "app_config.h"
#include "app_pins.h"
#include "app_state.h"

/* PA15 drives a 5V ACTIVE buzzer. Never drive PC13 (ARM input).
 * Patterns run without sleeps inside the HMI iteration. */
static uint8_t alert_kind(app_state_t st, uint8_t enabled, uint8_t arm, uint8_t ready)
{
    if (st == APP_STATE_FAULT_LOCK) return 4;
    if (st == APP_STATE_DECEL) return 5;
    if (st == APP_STATE_READY && enabled)
    {
        if (!arm) return 1;  /* one short beep: press ARM now */
        return ready ? 3 : 2; /* two beeps: neutral wait; long beep: pedal ready */
    }
    return 0;
}
static uint8_t alert_level(uint8_t kind, uint32_t elapsed)
{
    uint32_t phase;
    switch (kind)
    {
    case 1: return elapsed < 120u;
    case 2: return elapsed < 100u || (elapsed >= 200u && elapsed < 300u);
    case 3: return elapsed < 400u;
    case 4:
        phase = elapsed % 5000u;
        return phase < 150u || (phase >= 300u && phase < 450u) ||
               (phase >= 600u && phase < 750u);
    case 5: return elapsed % 1000u < 150u;
    default: return 0;
    }
}

void HmiTask(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    sensor_data_t s;
    limit_cfg_t lim;
    uint32_t ticks = 0, faults, since = HAL_GetTick();
    uint8_t kind = 0, next, arm;
    (void)arg;
    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_HMI_MS));
        ++ticks;
        (void)xQueuePeek(g_q_sensor, &s, 0);
        App_GetLimitSnapshot(&lim);
        faults = (uint32_t)xEventGroupGetBits(g_eg_fault) | g_local_faults;
        arm = HAL_GPIO_ReadPin(ARM_OK_GPIO_Port, ARM_OK_Pin) == GPIO_PIN_SET;
        next = alert_kind(g_app_state, g_drive_allowed, arm, g_arm_can_run);
        if (next != kind)
        {
            kind = next; since = HAL_GetTick();
            App_Logf("arm: prompt=%u gate=%u feedback=%u pedal_ready=%u\r\n",
                (unsigned)kind, (unsigned)g_drive_allowed, (unsigned)arm, (unsigned)g_arm_can_run);
        }
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin,
            alert_level(kind, HAL_GetTick() - since) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        if (ticks % 30u == 0)
            App_Logf("[%lu] st=%s v=%u.%02u lim=%u gate=%u arm=%u ready=%u f=0x%04X "
                     "seat=%u belt=%u helm=%u sensors=%u%u%u speed_valid=%u heap=%u\r\n",
                (unsigned long)(ticks * PERIOD_HMI_MS), App_State_Name(g_app_state),
                (unsigned)(s.speed_kmh_x100 / 100u), (unsigned)(s.speed_kmh_x100 % 100u),
                (unsigned)lim.effective, (unsigned)g_drive_allowed, (unsigned)arm, (unsigned)g_arm_can_run,
                (unsigned)(faults & FAULT_MASK_ALL), (unsigned)s.seat_pressed,
                (unsigned)s.belt_closed, (unsigned)s.helmet_ok,
                (unsigned)s.imu_present, (unsigned)s.tof_present, (unsigned)s.nfc_present,
                (unsigned)s.speed_valid, (unsigned)xPortGetFreeHeapSize());
    }
}
