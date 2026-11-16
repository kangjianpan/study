#include "app_main.h"
#include "app_config.h"
#include "app_state.h"
#include "bsp.h"
#include "protocol.h"

/* TelemetryTask，周期 100 ms
 *
 * 把 14 字节遥测帧打包好，走 USART3 交给 ESP32。
 * 由 ESP32 那边转成 BLE 通知，本文件完全不需要知道 BLE 的存在。
 *
 * 每次发送都带超时：ESP32 死掉不能把本任务卡住。
 * 而且它优先级只有 2，永远挤不到安全相关的任务前面去。 */

/* 遥测帧序号。每次发包后自增，接收端用来识别重传和丢包。
 * 8 位，255 之后回绕到 0 —— 协议里已经没有大小比较，
 * 所以回绕本身不会出问题，见 app_comm.c 的 seq_accept()。 */
static uint8_t s_seq = 0u;

static void pack_telemetry(uint8_t *p)
{
    sensor_data_t s;            /* 传感器快照 */
    limit_cfg_t   lim;          /* 限速快照 */
    uint32_t      faults;       /* 事件组里的故障位（只反映远端命令类故障，
                                 * 本地算出来的那些由 SafetyTask 直接进状态机） */
    uint8_t       wear;         /* 佩戴状态位域，打包进第 7 字节 */

    (void)xQueuePeek(g_q_sensor, &s, 0);
    App_GetLimitSnapshot(&lim);
    faults = (uint32_t)xEventGroupGetBits(g_eg_fault) | g_local_faults;

    p[0]  = (uint8_t)(s.speed_kmh_x100 & 0xFFu);
    p[1]  = (uint8_t)(s.speed_kmh_x100 >> 8);
    p[2]  = lim.effective;
    p[3]  = (uint8_t)(faults & 0xFFu);
    p[4]  = 0u;                                    /* 单圈时间，暂未实现 */
    p[5]  = 0u;
    p[6]  = (uint8_t)((s.bus_mv > 25500u) ? 255u : s.bus_mv / 100u); /* Legacy 0.1V, saturated. */

    wear = 0u;
    if (s.seat_pressed != 0u)
    {
        wear |= (uint8_t)(1u << WEAR_SEAT_SHIFT);
    }
    if (s.belt_closed != 0u)
    {
        wear |= (uint8_t)(1u << WEAR_BELT_SHIFT);
    }
    if (s.helmet_ok != 0u)
    {
        wear |= (uint8_t)(1u << WEAR_HELMET_SHIFT);
    }
    p[7]  = wear;

    p[8]  = 0u;                                    /* 里程，暂未实现 */
    p[9]  = 0u;
    p[10] = (uint8_t)g_app_state;                  /* 状态机当前状态，见文档 7.5 */
    p[11] = (uint8_t)((faults >> 8) & 0xFFu);
    p[12] = (uint8_t)(s.bus_mv & 0xFFu); /* v2: full voltage in mV, little endian. */
    p[13] = (uint8_t)(s.bus_mv >> 8);
}

void TelemetryTask(void *arg)
{
    TickType_t last_wake;   /* 100 ms 周期基准 */
    TickType_t last_hb;     /* 上一次发心跳的时刻，用来凑 1 s 间隔 */
    uint8_t    buf[PROTO_MAX_FRAME];        /* 整帧输出缓冲：帧头 + 载荷 + CRC */
    uint8_t    payload[PROTO_TELEMETRY_LEN];/* 14 字节遥测载荷，不含帧头 */
    uint8_t    hb;          /* 心跳载荷：bit0 = 当前是否允许驱动 */
    uint16_t   n;           /* Protocol_Build 返回的帧长；0 表示构造失败 */

    (void)arg;

    last_wake = xTaskGetTickCount();
    last_hb   = last_wake;

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PERIOD_TELEMETRY_MS));

        pack_telemetry(payload);
        n = Protocol_Build(buf, sizeof(buf), s_seq, CMD_TELEMETRY,
                           payload, PROTO_TELEMETRY_LEN);
        s_seq++;
        if (n > 0u)
        {
            (void)HAL_UART_Transmit(&huart3, buf, n, 10u);
        }

        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(LINK_HB_PERIOD_MS))
        {
            last_hb = xTaskGetTickCount();

            hb = (g_drive_allowed != 0u) ? 0x01u : 0x00u;
            n = Protocol_Build(buf, sizeof(buf), s_seq, CMD_HEARTBEAT, &hb, 1u);
            s_seq++;
            if (n > 0u)
            {
                (void)HAL_UART_Transmit(&huart3, buf, n, 10u);
            }
        }
    }
}
