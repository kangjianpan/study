#include "app_main.h"
#include "app_config.h"
#include "app_state.h"

#include <stdio.h>
#include <stdarg.h>

/* 黑匣子和调试控制台
 *
 * App_LogEvent 可以从任何任务里调，包括 SafetyTask：
 * 它只往队列里塞一条就返回，从不阻塞。
 * 真正打印的是 LogTask —— 优先级最低的那个任务，
 * 所以就算事件一下涌上来，也拖不慢控制回路。 */

/* 黑匣子本体：64 条 log_event_t 的环形记录，纯 RAM，掉电即失。
 * 这只是"最近发生了什么"的快照，不是持久化存储；
 * 以后要真正落 Flash 的话，LogTask 里存 s_box 那一处就是接入点。
 *
 * s_box    环形记录本体，LOGBOX_DEPTH 条
 * s_head   下一条要写的位置（也等于已写入的总条数对深度取模）
 * s_count  已存条数，上限 LOGBOX_DEPTH；满了之后保持满值不再增长
 *
 * 只用 LogTask 一个任务读写，所以不需要锁。 */
static log_event_t s_box[LOGBOX_DEPTH];
static uint16_t    s_head = 0u;
static uint16_t    s_count = 0u;

void App_LogEvent(uint8_t code, uint16_t value)
{
    log_event_t ev;

    if (g_q_log == NULL)
    {
        return;
    }

    ev.tick_ms  = (uint32_t)xTaskGetTickCount();
    ev.code     = code;
    ev.value    = value;
    ev.reserved = 0u;

    (void)xQueueSend(g_q_log, &ev, 0);
}

void App_Logf(const char *fmt, ...)
{
    va_list ap;

    if (g_mtx_uart_log == NULL)
    {
        return;
    }

    if (xSemaphoreTake(g_mtx_uart_log, pdMS_TO_TICKS(20)) != pdTRUE)
    {
        return;                 /* 抢不到锁就丢掉这一条，绝不阻塞调用者 */
    }

    va_start(ap, fmt);
    (void)vprintf(fmt, ap);
    va_end(ap);

    (void)xSemaphoreGive(g_mtx_uart_log);
}

uint16_t App_LogCount(void)
{
    return s_count;
}

/* 回读黑匣子：back = 0 表示最新一条。
 * 想回溯的条数超过已存条数时返回 NULL。
 * 以后做调试命令时会用到。 */
const log_event_t *App_LogGet(uint16_t back)
{
    uint16_t idx;

    if ((s_count == 0u) || (back >= s_count))
    {
        return NULL;
    }

    idx = (uint16_t)((s_head + LOGBOX_DEPTH - 1u - back) % LOGBOX_DEPTH);

    return &s_box[idx];
}

void LogTask(void *arg)
{
    log_event_t ev;

    (void)arg;

    for (;;)
    {
        if (xQueueReceive(g_q_log, &ev, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        s_box[s_head] = ev;
        s_head = (uint16_t)((s_head + 1u) % LOGBOX_DEPTH);
        if (s_count < LOGBOX_DEPTH)
        {
            s_count++;
        }

        switch (ev.code)
        {
        case LOG_EVT_FAULT_SET:
            App_Logf("[%lu] FAULT ON  0x%03X\r\n",
                     (unsigned long)ev.tick_ms, (unsigned)ev.value);
            break;

        case LOG_EVT_FAULT_CLEAR:
            App_Logf("[%lu] FAULT OFF 0x%03X\r\n",
                     (unsigned long)ev.tick_ms, (unsigned)ev.value);
            break;

        case LOG_EVT_LIMIT_CHANGE:
            App_Logf("[%lu] LIMIT src=%u eff=%u\r\n",
                     (unsigned long)ev.tick_ms,
                     (unsigned)((ev.value >> 8) & 0xFFu),
                     (unsigned)(ev.value & 0xFFu));
            break;

        case LOG_EVT_REMOTE_CMD:
            App_Logf("[%lu] CMD 0x%02X\r\n",
                     (unsigned long)ev.tick_ms, (unsigned)ev.value);
            break;

        case LOG_EVT_STATE:
            App_Logf("[%lu] STATE %s -> %s\r\n",
                     (unsigned long)ev.tick_ms,
                     App_State_Name((app_state_t)((ev.value >> 8) & 0xFFu)),
                     App_State_Name((app_state_t)(ev.value & 0xFFu)));
            break;

        default:
            break;
        }
    }
}
