#include "app_main.h"
#include "app_config.h"
#include "app_pins.h"
#include "bsp.h"
#include "drv_imu.h"
#include "drv_tof.h"
#include "drv_nfc.h"
#include "drv_bus.h"
#include "drv_wheel.h"
#include <string.h>

void SensorTask(void *arg)
{
    TickType_t last_wake;
    sensor_data_t s;
    int16_t acc[3], gyr[3];
    uint16_t dist[2], raw;
    uint32_t pedal_mv, current_ma, imu_tick = 0;
    uint8_t uid[10] = {0}, uid_len, uid_last[10] = {0}, uid_last_len = 0;
    uint8_t imu_hw, tof_hw, nfc_hw, imu_have = 0, tof_div = 0, nfc_div = 0;
    int status;
    uint8_t adc_valid;
    (void)arg;
    memset(&s, 0, sizeof(s));
    status = DrvBus_Init();
    DrvWheel_Init();
    imu_hw = status == DRV_OK && DrvImu_Init() == DRV_OK;
    tof_hw = status == DRV_OK && DrvTof_Init() == DRV_OK;
    nfc_hw = status == DRV_OK && DrvNfc_Init() == DRV_OK;
    g_sensor_initializing = 0;
    App_Logf("sensor: imu=%u tof=%u nfc=%u; drive_commissioned=%u\r\n",
             (unsigned)imu_hw, (unsigned)tof_hw, (unsigned)nfc_hw, (unsigned)APP_DRIVE_COMMISSIONED);
    last_wake = xTaskGetTickCount();
    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_SENSOR_MS));
        /* Advance IMU age only when a new measurement is actually received. */
        if (imu_hw)
        {
            status = DrvImu_Read(acc, gyr);
            if (status == DRV_OK)
            {
                memcpy(s.accel_mg, acc, sizeof(acc));
                memcpy(s.gyro_dps10, gyr, sizeof(gyr));
                imu_tick = xTaskGetTickCount(); imu_have = 1;
            }
            else if (status == DRV_ERR) imu_hw = 0;
        }
        s.imu_present = imu_hw && imu_have &&
            (xTaskGetTickCount() - imu_tick) <= pdMS_TO_TICKS(IMU_STALE_MS);
        if (++tof_div >= 3)
        {
            tof_div = 0;
            if (tof_hw)
            {
                status = DrvTof_Read(dist);
                if (status == DRV_OK)
                { s.tof_mm[0] = dist[0]; s.tof_mm[1] = dist[1]; s.tof_present = 1; }
                else if (status == DRV_ERR)
                { tof_hw = 0; s.tof_present = 0; s.tof_mm[0] = 0; s.tof_mm[1] = 0; }
            }
        }
        if (++nfc_div >= 5)
        {
            nfc_div = 0; s.helmet_ok = 0;
            if (nfc_hw)
            {
                memset(uid, 0, sizeof(uid));
                status = DrvNfc_PollTag(uid, &uid_len);
                if (status == DRV_ERR) nfc_hw = 0;
                else if (status == DRV_OK)
                {
                    s.helmet_ok = (uint8_t)DrvNfc_IsAuthorized(uid, uid_len);
                    if (uid_last_len != uid_len || memcmp(uid_last, uid, uid_len))
                    {
                        memcpy(uid_last, uid, uid_len); uid_last_len = uid_len;
                        App_Logf("nfc: len=%u uid=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X allowed=%u\r\n",
                            (unsigned)uid_len, uid[0],uid[1],uid[2],uid[3],uid[4],
                            uid[5],uid[6],uid[7],uid[8],uid[9],(unsigned)s.helmet_ok);
                    }
                }
            }
            s.nfc_present = nfc_hw;
            s.belt_closed = HAL_GPIO_ReadPin(BELT_GPIO_Port, BELT_Pin) == GPIO_PIN_RESET;
        }
        adc_valid = 1;
        raw = Bsp_Adc_Read(ADC_CH_CURRENT);
        if (raw > ADC_FULL_SCALE) { adc_valid = 0; raw = CURR_ZERO_COUNT; }
        current_ma = raw >= CURR_ZERO_COUNT ? raw - CURR_ZERO_COUNT : CURR_ZERO_COUNT - raw;
        current_ma *= CURRENT_MA_PER_COUNT;
        s.current_ma = (uint16_t)(current_ma > 65535u ? 65535u : current_ma);
        raw = Bsp_Adc_Read(ADC_CH_VBUS);
        if (raw > ADC_FULL_SCALE) { adc_valid = 0; raw = 0; }
        s.bus_mv = (uint16_t)((uint32_t)raw * ADC_VREF_MV * VBUS_DIVIDER_X10 /
                             (ADC_FULL_SCALE * 10u));
        raw = Bsp_Adc_Read(ADC_CH_PEDAL);
        if (raw > ADC_FULL_SCALE) { adc_valid = 0; raw = 0; }
        pedal_mv = (uint32_t)raw * ADC_VREF_MV * 10u / (ADC_FULL_SCALE * 6u);
        s.pedal_fault = pedal_mv < PEDAL_FAULT_LOW_MV || pedal_mv > PEDAL_FAULT_HIGH_MV;
        if (s.pedal_fault || pedal_mv <= PEDAL_IDLE_MV) s.throttle_permille = 0;
        else if (pedal_mv >= PEDAL_FULL_MV) s.throttle_permille = 1000;
        else s.throttle_permille = (uint16_t)((pedal_mv - PEDAL_IDLE_MV) * 1000u /
                                             (PEDAL_FULL_MV - PEDAL_IDLE_MV));
        raw = Bsp_Adc_Read(ADC_CH_SEAT);
        if (raw > ADC_FULL_SCALE) { adc_valid = 0; raw = 0; }
        s.seat_left_pressed = raw > 1500u;
        raw = Bsp_Adc_Read(ADC_CH_SEAT_R);
        if (raw > ADC_FULL_SCALE) { adc_valid = 0; raw = 0; }
        s.seat_right_pressed = raw > 1500u;
        s.seat_pressed = s.seat_left_pressed | s.seat_right_pressed;
        s.speed_valid = DrvWheel_Read(&s.speed_kmh_x100, &s.hall_pulses) == DRV_OK;
        s.sample_tick = xTaskGetTickCount(); s.sample_valid = adc_valid;
        (void)xQueueOverwrite(g_q_sensor, &s);
    }
}
