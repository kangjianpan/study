#include "drv_bus.h"
#include "app_main.h"
#include "app_pins.h"
#include "drv_imu.h"

static SPI_HandleTypeDef s_spi;
static I2C_HandleTypeDef s_i2c;
static uint8_t s_ready;

int DrvBus_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    if (s_ready) return DRV_OK;
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &gpio);
    s_spi.Instance = SPI1;
    s_spi.Init.Mode = SPI_MODE_MASTER;
    s_spi.Init.Direction = SPI_DIRECTION_2LINES;
    s_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_spi.Init.NSS = SPI_NSS_SOFT;
    s_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
    s_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_spi.Init.CRCPolynomial = 7;
    s_i2c.Instance = I2C1;
    s_i2c.Init.ClockSpeed = 400000;
    s_i2c.Init.DutyCycle = I2C_DUTYCYCLE_2;
    s_i2c.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    s_i2c.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    s_i2c.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    s_i2c.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_SPI_Init(&s_spi) != HAL_OK || HAL_I2C_Init(&s_i2c) != HAL_OK)
        return DRV_ERR;
    s_ready = 1u;
    return DRV_OK;
}

static int spi_io(uint8_t dev, uint8_t cmd, uint8_t *data, uint16_t n, uint8_t read)
{
    HAL_StatusTypeDef status;
    uint8_t ignored;
    uint16_t cs = dev ? GPIO_PIN_2 : GPIO_PIN_4;
    if (!s_ready || dev > 1 || !data || !n ||
        xSemaphoreTake(g_mtx_i2c, pdMS_TO_TICKS(2)) != pdTRUE) return DRV_ERR;
    HAL_GPIO_WritePin(GPIOA, cs, GPIO_PIN_RESET);
    status = HAL_SPI_TransmitReceive(&s_spi, &cmd, &ignored, 1, 3);
    if (status == HAL_OK)
    {
        if (read) status = HAL_SPI_Receive(&s_spi, data, n, 3);
        else
        {
            /* Drain full-duplex RX on every write. */
            while (n-- && status == HAL_OK)
                status = HAL_SPI_TransmitReceive(&s_spi, data++, &ignored, 1, 3);
        }
    }
    HAL_GPIO_WritePin(GPIOA, cs, GPIO_PIN_SET);
    (void)xSemaphoreGive(g_mtx_i2c);
    return status == HAL_OK ? DRV_OK : DRV_ERR;
}
int DrvBus_SpiRead(uint8_t d, uint8_t c, uint8_t *p, uint16_t n)
{ return spi_io(d, c, p, n, 1); }
int DrvBus_SpiWrite(uint8_t d, uint8_t c, const uint8_t *p, uint16_t n)
{ return spi_io(d, c, (uint8_t *)p, n, 0); }

static int i2c_io(uint8_t a, uint16_t r, uint8_t *p, uint16_t n, uint8_t read)
{
    HAL_StatusTypeDef status;
    if (!s_ready || !p || !n || a > 0x7f ||
        xSemaphoreTake(g_mtx_i2c, pdMS_TO_TICKS(2)) != pdTRUE) return DRV_ERR;
    if (read) status = HAL_I2C_Mem_Read(&s_i2c, (uint16_t)(a << 1), r, I2C_MEMADD_SIZE_16BIT, p, n, 5);
    else status = HAL_I2C_Mem_Write(&s_i2c, (uint16_t)(a << 1), r, I2C_MEMADD_SIZE_16BIT, p, n, 5);
    (void)xSemaphoreGive(g_mtx_i2c);
    return status == HAL_OK ? DRV_OK : DRV_ERR;
}
int DrvBus_I2cRead(uint8_t a, uint16_t r, uint8_t *p, uint16_t n)
{ return i2c_io(a, r, p, n, 1); }
int DrvBus_I2cWrite(uint8_t a, uint16_t r, const uint8_t *p, uint16_t n)
{ return i2c_io(a, r, (uint8_t *)p, n, 0); }
uint32_t DrvBus_Now(void) { return HAL_GetTick(); }
void DrvBus_Delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
void DrvBus_TofShutdown(uint8_t i, uint8_t on)
{
    HAL_GPIO_WritePin(GPIOB, i ? TOF_XSHUT2_Pin : TOF_XSHUT1_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
void DrvBus_NfcReset(uint8_t on)
{ HAL_GPIO_WritePin(NFC_RST_GPIO_Port, NFC_RST_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET); }
