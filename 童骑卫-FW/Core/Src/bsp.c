#include "bsp.h"
#include "app_config.h"
#include "app_pins.h"
#include "usart.h"

/* 这里承担 USART3 / ADC / TIM1 的 MspInit 工作，替代 CubeMX 生成的版本。
 * usart.c 里重写的 HAL_UART_MspInit 只处理 USART1，
 * 所以 USART3 的引脚复用必须在 HAL_UART_Init 之前在这里配好。 */

/* USART3 句柄：与 ESP32 的链路。
 * 对 ESP32 那边就是一条普通串口，本工程从不把它当调试串口用。
 * 属性由 Bsp_Usart3_Init() 填，没有走 CubeMX。 */
UART_HandleTypeDef huart3;

/* ADC1 句柄：电流(PA0) / 母线电压(PA1) / 踏板(PA3) / 座垫(PB0) 共用。
 * 单通道轮询，每读一路都要重新配置一次通道，见 Bsp_Adc_Read()。 */
ADC_HandleTypeDef  hadc1;

/* GPIO */
void Bsp_Gpio_Init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 在把引脚切成输出之前就先把电平预置成安全值，
     * 这样开机过程中电调不会看到任何脉冲。 */
    HAL_GPIO_WritePin(MOTOR_EN_GPIO_Port, MOTOR_EN_Pin,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(HB_GPIO_Port,       HB_Pin,        GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port,   BUZZER_Pin,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2 | GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, TOF_XSHUT1_Pin | TOF_XSHUT2_Pin | NFC_RST_Pin, GPIO_PIN_RESET);

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = MOTOR_EN_Pin;
    HAL_GPIO_Init(MOTOR_EN_GPIO_Port, &gpio);
    gpio.Pin = HB_Pin;
    HAL_GPIO_Init(HB_GPIO_Port, &gpio);
    gpio.Pin = BUZZER_Pin;
    HAL_GPIO_Init(BUZZER_GPIO_Port, &gpio);
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = TOF_XSHUT1_Pin | TOF_XSHUT2_Pin | NFC_RST_Pin;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = ARM_OK_Pin;
    HAL_GPIO_Init(ARM_OK_GPIO_Port, &gpio);
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin = ESTOP_Pin;
    HAL_GPIO_Init(ESTOP_GPIO_Port, &gpio);
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = BELT_Pin;
    HAL_GPIO_Init(BELT_GPIO_Port, &gpio);
}

/* USART3：与 ESP32 通信 */
void Bsp_Usart3_Init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_10;              /* 发送 */
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);

    gpio.Pin  = GPIO_PIN_11;               /* 接收 */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);

    huart3.Instance          = USART3;
    huart3.Init.BaudRate     = 115200;
    huart3.Init.WordLength   = UART_WORDLENGTH_8B;
    huart3.Init.StopBits     = UART_STOPBITS_1;
    huart3.Init.Parity       = UART_PARITY_NONE;
    huart3.Init.Mode         = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    (void)HAL_UART_Init(&huart3);

    HAL_NVIC_SetPriority(USART3_IRQn, 11, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
}

/* ADC1：电流(PA0)、母线电压(PA1)、踏板(PA3)、座垫压力片(PB0)
 *
 * 一次只转换一个通道，用轮询方式读。
 * 按 10 ms 的周期算，每个周期花不到 0.2 ms。 */
void Bsp_Adc_Init(void)
{
    GPIO_InitTypeDef   gpio;
    ADC_ChannelConfTypeDef cfg;

    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);   /* 72/6 = 12 MHz，F103 的 ADC 上限 14 MHz */

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOB, &gpio);

    hadc1.Instance                   = ADC1;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    (void)HAL_ADC_Init(&hadc1);

    (void)HAL_ADCEx_Calibration_Start(&hadc1);

    /* 第一次读之前先把一个通道配好，否则转换出来的值没有意义 */
    cfg.Channel      = ADC_CH_CURRENT;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
    (void)HAL_ADC_ConfigChannel(&hadc1, &cfg);
}

uint16_t Bsp_Adc_Read(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg;
    uint16_t value = 0u;

    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK)
    {
        return 0u;
    }

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
        return 0u;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 5u) == HAL_OK)
    {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    (void)HAL_ADC_Stop(&hadc1);

    return value;
}

/* 独立看门狗
 *
 * LSI 约 40 kHz，除 4 得 10 kHz，重装值 2000 -> 超时 200 ms。
 * 全工程只有 SafetyTask 喂狗。这个任务一旦停了（卡死、死锁，或被人为注入故障），
 * MCU 就会复位，电调使能线则回落到外部下拉电阻决定的低电平。
 *
 * 注意 IWDG 靠 LSI 独立计数，不需要 CPU、也不需要中断，
 * 所以即使代码里把中断全关了，它照样会把芯片复位掉。 */
void Bsp_Iwdg_Init(void)
{
#if APP_USE_IWDG
    /* 故意直接操作寄存器：这棵 HAL 树里根本没带 hal_iwdg.c。
     * IWDG 由 LSI 驱动，而 LSI 上电就自动起振。 */
    IWDG->KR = 0x5555u;                 /* 解锁 PR 和 RLR */
    IWDG->PR = 0x0000u;                 /* LSI / 4，约 10 kHz */
    IWDG->RLR = 2000u;                  /* 2000 个计数 -> 200 ms */
    while (IWDG->SR != 0u)
    {
        /* 等新写入的重装值被硬件接受 */
    }
    IWDG->KR = 0xAAAAu;                 /* 先装载一次计数器 */
    IWDG->KR = 0xCCCCu;                 /* 启动，之后再也关不掉 */
#endif
}

void Bsp_Iwdg_Feed(void)
{
#if APP_USE_IWDG
    IWDG->KR = 0xAAAAu;
#endif
}

uint8_t Bsp_GetResetReason(void)
{
    uint8_t reason = RESET_UNKNOWN;

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
    {
        reason = RESET_IWDG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)
    {
        reason = RESET_PIN;
    }
    else
    {
        reason = RESET_UNKNOWN;
    }

    return reason;
}

void Bsp_ClearResetFlags(void)
{
    __HAL_RCC_CLEAR_RESET_FLAGS();
}
