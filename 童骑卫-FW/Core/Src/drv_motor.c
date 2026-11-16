#include "drv_motor.h"
#include "app_config.h"
#include "app_pins.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/* 电调驱动，TIM1_CH1，输出在 PA8
 *
 * 72 MHz / 72 = 1 MHz 计数频率，周期取 20000 -> 一帧 50 Hz。
 * 脉宽 1000..2000 us，1500 us 是中立位。
 *
 * 安全提示：只有在 MOTOR_EN 为高时，电调信号线才算是可信的。
 * MOTOR_EN 由底板安全门控、手动锁存及外部看门狗共同限制。
 * 下拉电阻只在 MCU 复位/高阻时有效，不能检测 CPU 停滞。 */

/* TIM1 的句柄。只有本文件用，所以不用暴露到头文件里。
 * 和 CubeMX 生成的 htimX 不冲突：那套是给 TIM4 做 HAL 时基用的。 */
static TIM_HandleTypeDef htim1;

/* 当前油门指令，千分比 0..1000，跨调用保留。
 * 存在的唯一理由是给 SafetyTask 用：DECEL 状态要判断
 * "斜坡是否真的降到 0 了"才能回到 READY，而它只能通过
 * Motor_GetOutput() 读到这个值。 */
static volatile int16_t s_output = 0;
static volatile uint8_t s_enabled = 0u;

void Motor_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC;
    GPIO_InitTypeDef   gpio;

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_8;
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 72u - 1u;
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 20000u - 1u;
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0u;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    (void)HAL_TIM_PWM_Init(&htim1);

    sConfigOC.OCMode       = TIM_OCMODE_PWM1;
    sConfigOC.Pulse        = ESC_PULSE_NEUTRAL_US;
    sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    (void)HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);

    (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    __HAL_TIM_MOE_DISABLE(&htim1);

    s_output = 0;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ESC_PULSE_NEUTRAL_US);
}

void Motor_Enable(uint8_t on)
{
    taskENTER_CRITICAL();
    if (on != 0u)
    {
        if (s_enabled == 0u)
        {
            s_output = 0;
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ESC_PULSE_NEUTRAL_US);
            /* Transfer neutral from the preload register before releasing MOE. */
            htim1.Instance->EGR = TIM_EGR_UG;
            __HAL_TIM_MOE_ENABLE(&htim1);
            s_enabled = 1u;
        }
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_SET);
    }
    else
    {
        HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin, GPIO_PIN_RESET);
        s_enabled = 0u;
        s_output = 0;
        __HAL_TIM_MOE_DISABLE(&htim1);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ESC_PULSE_NEUTRAL_US);
    }
    taskEXIT_CRITICAL();
}

void Motor_SetOutput(int16_t permille)
{
    int32_t  pulse;

    if (permille > 1000)
    {
        permille = 1000;
    }
    if (permille < -1000)
    {
        permille = -1000;
    }

    pulse = (int32_t)ESC_PULSE_NEUTRAL_US + ((int32_t)permille * 500) / 1000;
    if (pulse < (int32_t)ESC_PULSE_MIN_US)
    {
        pulse = (int32_t)ESC_PULSE_MIN_US;
    }
    if (pulse > (int32_t)ESC_PULSE_MAX_US)
    {
        pulse = (int32_t)ESC_PULSE_MAX_US;
    }

    taskENTER_CRITICAL();
    if (s_enabled != 0u)
    {
        s_output = permille;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)pulse);
    }
    else
    {
        s_output = 0;
        __HAL_TIM_MOE_DISABLE(&htim1);
    }
    taskEXIT_CRITICAL();
}

void Motor_Stop(void)
{
    Motor_SetOutput(0);
}

int16_t Motor_GetOutput(void)
{
    return s_output;
}
