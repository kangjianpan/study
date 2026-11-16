#include "app_main.h"
#include "app_config.h"
#include "app_pins.h"
#include "bsp.h"

#include "drv_imu.h"
#include "drv_tof.h"
#include "drv_nfc.h"

#include <string.h>

/* SensorTask，周期 10 ms
 *
 * 把一份 sensor_data_t 写进长度 1 的队列。下游都用 Peek 读，
 * 所以每个消费者随时都能拿到最新的一次采样，
 * 既不用来回拷贝，也不会把生产者卡住。
 *
 * ToF 每 3 拍读一次（30 ms），NFC 每 5 拍一次（50 ms）：
 * 这两颗器件的转换时间都比 10 ms 的节拍长。 */

void SensorTask(void *arg)
{
    TickType_t    last_wake;    /* vTaskDelayUntil 的基准，锁住 10 ms 周期 */
    sensor_data_t s;            /* 正在往上写的这一份采样，写完整份才覆盖进队列 */
    int16_t       acc[3];       /* IMU 加速度临时缓冲，mg */
    int16_t       gyr[3];       /* IMU 角速度临时缓冲，0.1 °/s */
    uint16_t      dist[2];      /* ToF 临时缓冲，mm */
    uint8_t       uid[4];       /* 读到的头盔标签 UID 临时缓冲；本文件只关心"读到没有" */
    uint32_t      pedal_mv;
    uint32_t      current_ma;
    uint16_t      raw;          /* ADC 原始码值，各通道轮流复用 */
    uint8_t       tof_div;      /* ToF 分频计数：10 ms 一拍，数到 3 才是 30 ms */
    uint8_t       nfc_div;      /* 佩戴分频计数：数到 5 才是 50 ms */

    (void)arg;

    memset(&s, 0, sizeof(s));
    s.accel_mg[2] = 1000;

    s.imu_present = (DrvImu_Init() == DRV_OK) ? 1u : 0u;
    s.tof_present = (DrvTof_Init() == DRV_OK) ? 1u : 0u;
    s.nfc_present = (DrvNfc_Init() == DRV_OK) ? 1u : 0u;

    dist[0] = 4000u;
    dist[1] = 4000u;
    tof_div = 0u;
    nfc_div = 0u;

    App_Logf("sensor: imu=%u tof=%u nfc=%u\r\n",
             (unsigned)s.imu_present,
             (unsigned)s.tof_present,
             (unsigned)s.nfc_present);

    last_wake = xTaskGetTickCount();

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_SENSOR_MS));

        /* IMU */
        if (s.imu_present != 0u)
        {
            if (DrvImu_Read(acc, gyr) == DRV_OK)
            {
                s.accel_mg[0] = acc[0];
                s.accel_mg[1] = acc[1];
                s.accel_mg[2] = acc[2];
                s.gyro_dps10[0] = gyr[0];
                s.gyro_dps10[1] = gyr[1];
                s.gyro_dps10[2] = gyr[2];
            }
            else
            {
                s.imu_present = 0u;
            }
        }

        /* ToF，每 30 ms 读一次 */
        tof_div++;
        if (tof_div >= 3u)
        {
            tof_div = 0u;
            if (s.tof_present != 0u)
            {
                if (DrvTof_Read(dist) == DRV_OK)
                {
                    s.tof_mm[0] = dist[0];
                    s.tof_mm[1] = dist[1];
                }
                else
                {
                    s.tof_present = 0u;
                    s.tof_mm[0] = 0u;
                    s.tof_mm[1] = 0u;
                }
            }
            else
            {
                s.tof_mm[0] = 0u;
                s.tof_mm[1] = 0u;
            }
        }

        /* 头盔标签，每 50 ms 读一次 */
        nfc_div++;
        if (nfc_div >= 5u)
        {
            nfc_div = 0u;
            if (s.nfc_present != 0u)
            {
                s.helmet_ok = (DrvNfc_PollTag(uid) == DRV_OK) ? 1u : 0u;
            }
            else
            {
                s.helmet_ok = 0u;
            }

            /* 安全带开关跟着标签一起读，省一次来回 */
            s.belt_closed =
                (HAL_GPIO_ReadPin(BELT_GPIO_Port, BELT_Pin) == GPIO_PIN_RESET)
                    ? 1u : 0u;
        }

        /* 模拟前端 */
        raw = Bsp_Adc_Read(ADC_CH_CURRENT);
        /* ACS712 零电流时输出 2.5 V（过 R28/R29 分压后是 1.5 V），
         * 所以要先扣掉这个零点。它是双向器件，低于零点对应的就是负电流，
         * 这里一律钳到 0。 */
        /* Bipolar sensor: protect against either current direction; saturate. */
        current_ma = (raw >= CURR_ZERO_COUNT) ? (raw - CURR_ZERO_COUNT)
                                              : (CURR_ZERO_COUNT - raw);
        current_ma *= CURRENT_MA_PER_COUNT;
        s.current_ma = (uint16_t)((current_ma > 65535u) ? 65535u : current_ma);

        raw = Bsp_Adc_Read(ADC_CH_VBUS);
        s.bus_mv = (uint16_t)((uint32_t)raw * ADC_VREF_MV * VBUS_DIVIDER_X10 /
                             (ADC_FULL_SCALE * 10u));

        raw = Bsp_Adc_Read(ADC_CH_PEDAL);
        pedal_mv = (uint32_t)raw * ADC_VREF_MV * 10u / (ADC_FULL_SCALE * 6u);
        s.pedal_fault = (pedal_mv < PEDAL_FAULT_LOW_MV ||
                         pedal_mv > PEDAL_FAULT_HIGH_MV) ? 1u : 0u;
        if (s.pedal_fault || pedal_mv <= PEDAL_IDLE_MV)
            s.throttle_permille = 0u;
        else if (pedal_mv >= PEDAL_FULL_MV)
            s.throttle_permille = 1000u;
        else
            s.throttle_permille = (uint16_t)((pedal_mv - PEDAL_IDLE_MV) * 1000u /
                                            (PEDAL_FULL_MV - PEDAL_IDLE_MV));

        raw = Bsp_Adc_Read(ADC_CH_SEAT);
        s.seat_left_pressed = (raw > 1500u) ? 1u : 0u;
        raw = Bsp_Adc_Read(ADC_CH_SEAT_R);
        s.seat_right_pressed = (raw > 1500u) ? 1u : 0u;
        s.seat_pressed = s.seat_left_pressed | s.seat_right_pressed;

        /* Wheel capture is not implemented. Never substitute zero for valid speed. */
        s.speed_kmh_x100 = 0u;
        s.speed_valid = 0u;
        s.hall_pulses = 0u;
        s.sample_tick = xTaskGetTickCount();
        s.sample_valid = 1u;

        (void)xQueueOverwrite(g_q_sensor, &s);
    }
}
