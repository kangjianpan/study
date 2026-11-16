#include "app_main.h"
#include "app_config.h"
#include "app_pins.h"
#include "app_state.h"

/* HmiTask，周期 33 ms
 *
 * 状态灯闪烁频率：
 *   允许驱动    1 Hz
 *   禁止驱动    5 Hz
 *
 * 每秒往调试串口打一行状态。
 * 打印内容全部是 ASCII —— printf 里写中文，ARMCC 按 GBK 读源码时会报
 * #8: missing closing quote，中文只写在注释里。 */

void HmiTask(void *arg)
{
    TickType_t    last_wake;    /* 33 ms 周期基准 */
    sensor_data_t s;            /* 只用来打印，不参与任何控制判断 */
    limit_cfg_t   lim;          /* 当前生效限速，同样只用于打印 */
    uint32_t      faults;       /* 事件组里的故障位，只用于打印 */
    uint32_t      ticks;        /* 自启动以来的拍数，同时充当闪烁相位和打印节流 */
    uint32_t      period;       /* 本次闪烁的半周期，单位是 33 ms 的拍数（2/3/15） */
    uint8_t       led;          /* 状态灯当前电平，每半周期取反 */

    (void)arg;

    last_wake = xTaskGetTickCount();
    ticks = 0u;
    led = 0u;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_HMI_MS));
        ticks++;

        (void)xQueuePeek(g_q_sensor, &s, 0);
        App_GetLimitSnapshot(&lim);
        faults = (uint32_t)xEventGroupGetBits(g_eg_fault) | g_local_faults;

        if (g_app_state == APP_STATE_DECEL)
        {
            period = 2u;                                /* 正在收油门，快闪 */
        }
        else
        {
            period = (g_drive_allowed != 0u) ? 15u : 3u; /* 单位是 33 ms 的拍数 */
        }
        /* PC13 is hardware ARM feedback input in v2. */
        (void)period;
        (void)led;

        if ((ticks % 30u) == 0u)                        /* 约 1 秒一次 */
        {
            App_Logf("[%lu] st=%s v=%u.%02u lim=%u run=%u f=0x%03X "
                     "seat=%u belt=%u helm=%u heap=%u\r\n",
                     (unsigned long)(ticks * PERIOD_HMI_MS),
                     App_State_Name(g_app_state),
                     (unsigned)(s.speed_kmh_x100 / 100u),
                     (unsigned)(s.speed_kmh_x100 % 100u),
                     (unsigned)lim.effective,
                     (unsigned)g_drive_allowed,
                     (unsigned)(faults & FAULT_MASK_ALL),
                     (unsigned)s.seat_pressed,
                     (unsigned)s.belt_closed,
                     (unsigned)s.helmet_ok,
                     (unsigned)xPortGetFreeHeapSize());
        }
    }
}
