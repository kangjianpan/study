#include "drv_wheel.h"
#include "app_config.h"
#include "drv_imu.h"
#ifndef HOST_TEST
#include "app_pins.h"
#include "FreeRTOS.h"
#include "task.h"
#else
#include "wheel_host.h"
#endif

static volatile uint32_t s_edges[2], s_last[2], s_period[2];
static volatile uint8_t s_seen[2], s_period_valid[2];

void DrvWheel_Init(void)
{
#ifndef HOST_TEST
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 12, 0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 12, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
#endif
}

void DrvWheel_Edge(uint8_t i, uint32_t now)
{
    uint32_t dt;
    if (i > 1) return;
    dt = now - s_last[i];
    if (s_seen[i] && dt < WHEEL_MIN_EDGE_MS) return;
    if (s_seen[i]) { s_period[i] = dt; s_period_valid[i] = 1; }
    s_seen[i] = 1; s_last[i] = now; ++s_edges[i];
}

int DrvWheel_Read(uint16_t *speed_x100, uint16_t *pulses)
{
    uint32_t now, last[2], period[2], count[2], age, dt, v[2] = {0,0};
    volatile uint8_t configured = APP_WHEEL_VALIDATED && WHEEL_CIRCUMFERENCE_MM && WHEEL_PULSES_PER_REV;
    uint8_t valid[2], i;
    if (!speed_x100 || !pulses) return DRV_ERR;
    *speed_x100 = 0; *pulses = 0;
    taskENTER_CRITICAL();
    now = HAL_GetTick();
    for (i = 0; i < 2; ++i)
    { last[i] = s_last[i]; period[i] = s_period[i]; count[i] = s_edges[i]; valid[i] = s_period_valid[i]; }
    taskEXIT_CRITICAL();
    *pulses = (uint16_t)(count[0] + count[1]);
    if (!configured) return DRV_ERR;
    for (i = 0; i < 2; ++i)
    {
        age = now - last[i];
        if (!valid[i] || age >= WHEEL_STOP_TIMEOUT_MS) continue;
        dt = age > period[i] ? age : period[i];
        if (!dt) return DRV_ERR;
        /* mm/ms -> km/h*100. 64 bits prevent overflow with sparse magnets. */
        v[i] = (uint32_t)((uint64_t)WHEEL_CIRCUMFERENCE_MM * 360u /
                         ((uint64_t)(WHEEL_PULSES_PER_REV ? WHEEL_PULSES_PER_REV : 1u) * dt));
        if (v[i] > WHEEL_MAX_KMH_X100) return DRV_ERR;
    }
    if ((v[0] > WHEEL_MISMATCH_MIN_X100 && v[1] == 0) ||
        (v[1] > WHEEL_MISMATCH_MIN_X100 && v[0] == 0)) return DRV_ERR;
    /* Use the faster wheel so cornering cannot hide overspeed. */
    *speed_x100 = (uint16_t)(v[0] > v[1] ? v[0] : v[1]);
    return DRV_OK;
}
#ifndef HOST_TEST
void EXTI4_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }
void EXTI9_5_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_5); }
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == GPIO_PIN_4) DrvWheel_Edge(0, HAL_GetTick());
    else if (pin == GPIO_PIN_5) DrvWheel_Edge(1, HAL_GetTick());
}
#endif
