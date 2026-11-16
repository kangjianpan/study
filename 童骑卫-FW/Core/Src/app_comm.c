#include "app_main.h"
#include "app_config.h"
#include "bsp.h"
#include "protocol.h"
#include "ringbuf.h"
#include "usart.h"

/* CommTask：链路层
 *
 * 中断里只做两件事——把字节塞进环形缓冲、唤醒本任务。
 * 所有拆帧工作都在任务上下文里做，
 * 这样哪怕数据流全乱了，也不会把中断或安全任务卡住。
 *
 * 两个互相独立的解析器，一条物理链路一个。
 * 如果硬塞进同一个状态机，一旦调试控制台和 ESP32 同时在发数据，帧就会串掉。 */

/* 每条链路一套：缓冲区 + 解析器 + 重复帧过滤器
 *
 * USART3（ESP32 / BLE 网关）是主链路：
 *   s_rb3_mem / s_rb3    128 字节环形缓冲 + 它的控制块
 *   s_rx3_byte           HAL_UART_Receive_IT 的落点，中断每收 1 字节就搬进 s_rb3
 *   s_parser3            这条链路的协议状态机（半帧状态不能和 USART1 共用）
 *   s_filter3            这条链路的 SEQ 去重与会话跟踪
 *
 * USART1（USB 调试口）是辅助链路，只在 APP_DEBUG_UART1_CMD=1 时启用：
 *   s_rb1_mem / s_rb1    64 字节，比主链路小一半——人工敲命令用不到那么大
 *   s_rx1_byte           同上，中断落点
 *   s_parser1            独立的状态机
 *   s_filter1            独立的过滤器
 *
 * 两条链路的所有缓冲都声明为 static，从 .bss 里分配，
 * 不占任何任务的栈——中断服务程序要写它们，放栈上就不可靠了。 */
#define RB_USART3_SIZE   128u
#define RB_USART1_SIZE   64u

static uint8_t        s_rb3_mem[RB_USART3_SIZE];
static uint8_t        s_rb1_mem[RB_USART1_SIZE];
static ringbuf_t      s_rb3;
static ringbuf_t      s_rb1;

static uint8_t        s_rx3_byte;
#if APP_DEBUG_UART1_CMD
static uint8_t        s_rx1_byte;
#endif

static proto_parser_t s_parser3;
static proto_parser_t s_parser1;

/* 重复帧抑制与会话跟踪，每条链路各一套过滤器
 *
 * 规则故意定得很简单，因为凡是更花哨的版本，在序号回绕这一点上都有洞：
 *
 *   - 同一会话内，SEQ 在 DUP_WINDOW_MS 之内重复出现，判为重传，直接丢掉；
 *   - 一条链路静默超过 LINK_TIMEOUT_MS 就算开启新会话，
 *     下一帧不管 SEQ 是多少都接受。
 *     正是这一条让手机或 ESP32 重启后能从 0 重新开始计数。
 *
 * 为什么不要求"SEQ 必须比上一帧大"：SEQ 只有 8 位、会回绕，
 * 任何大小比较都得搬出序列号运算，还要额外规定半个窗口怎么判，
 * 而在这个场景里一点好处都没有。
 * 同一条 BLE 连接内 ATT 写操作本身就是有序可靠的，
 * 真正的乱序只可能发生在重连前后，而那已经被会话复位覆盖了。
 *
 * 作用范围：过滤器按物理链路分，不按命令分。
 * 窗口内任意两帧只要 SEQ 相同，就当成同一帧处理。 */
#define DUP_WINDOW_MS   1000u

typedef struct
{
    uint8_t    last_seq;    /* 最近一帧的序号 */
    uint8_t    valid;       /* 0 = 还没见过任何帧，或刚判定为新会话；1 = 正常跟踪中 */
    TickType_t last_tick;   /* 最近一帧到达的节拍数 */
} seq_filter_t;

static seq_filter_t s_filter3;   /* USART3 的去重过滤器 */
#if APP_DEBUG_UART1_CMD
static seq_filter_t s_filter1;   /* USART1 的去重过滤器 */
#endif

static int seq_accept(seq_filter_t *f, uint8_t seq)
{
    TickType_t now = xTaskGetTickCount();   /* 本帧到达时刻 */
    TickType_t idle;                        /* 距离上一帧过了多久 */

    if (f->valid != 0u)
    {
        idle = now - f->last_tick;

        if (idle >= pdMS_TO_TICKS(LINK_TIMEOUT_MS))
        {
            f->valid = 0u;              /* 新会话，重新起算 */
        }
        else if ((seq == f->last_seq) && (idle < pdMS_TO_TICKS(DUP_WINDOW_MS)))
        {
            return 0;                   /* 重传，丢弃 */
        }
        else
        {
            /* 会话内的正常帧 */
        }
    }

    f->last_seq  = seq;
    f->last_tick = now;
    f->valid     = 1u;

    return 1;
}

void Comm_RxInit(void)
{
    RingBuf_Init(&s_rb3, s_rb3_mem, RB_USART3_SIZE);
    RingBuf_Init(&s_rb1, s_rb1_mem, RB_USART1_SIZE);

    Protocol_Init(&s_parser3);
    Protocol_Init(&s_parser1);

    (void)HAL_UART_Receive_IT(&huart3, &s_rx3_byte, 1u);
#if APP_DEBUG_UART1_CMD
    (void)HAL_UART_Receive_IT(&huart1, &s_rx1_byte, 1u);
#endif
}

/* USART3 的中断入口，由 USART3 向量跳进来。
 * 写在这里是因为 stm32f1xx_it.c 只提供了 USART1 的处理函数。 */
void USART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart3);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BaseType_t woken = pdFALSE;     /* 本中断是否唤醒了更高优先级任务，出中断时要据此让出 */

    if (huart->Instance == USART3)
    {
        (void)RingBuf_Put(&s_rb3, s_rx3_byte);
        (void)HAL_UART_Receive_IT(&huart3, &s_rx3_byte, 1u);
    }
#if APP_DEBUG_UART1_CMD
    else if (huart->Instance == USART1)
    {
        (void)RingBuf_Put(&s_rb1, s_rx1_byte);
        (void)HAL_UART_Receive_IT(&huart1, &s_rx1_byte, 1u);
    }
#endif
    else
    {
        return;
    }

    if (g_comm_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(g_comm_task_handle, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* 帧错误或溢出错误不能把接收通路弄死，重新挂上一次接收即可 */
    if (huart->Instance == USART3)
    {
        (void)HAL_UART_Receive_IT(&huart3, &s_rx3_byte, 1u);
    }
#if APP_DEBUG_UART1_CMD
    else if (huart->Instance == USART1)
    {
        (void)HAL_UART_Receive_IT(&huart1, &s_rx1_byte, 1u);
    }
#endif
}

static void handle_frame(const proto_frame_t *f)
{
    /* 只要收到一帧有效数据，就说明链路还活着 */
    g_last_link_tick = xTaskGetTickCount();
    g_link_seen = 1u;

    switch (f->cmd)
    {
    case CMD_SET_LIMIT:
        if (f->len >= 1u)
        {
            App_SetLimit(LIMIT_SRC_PARENT, f->payload[0]);
        }
        break;

    case CMD_SET_LIMIT_VENUE:
        if (f->len >= 1u)
        {
            App_SetLimit(LIMIT_SRC_VENUE, f->payload[0]);
        }
        break;

    case CMD_SET_LIMIT_CHILD:
        if (f->len >= 1u)
        {
            App_SetLimit(LIMIT_SRC_CHILD, f->payload[0]);
        }
        break;

    case CMD_STOP:
        (void)xEventGroupSetBits(g_eg_fault, FAULT_REMOTE_STOP);
        break;

    case CMD_RESUME:
        (void)xEventGroupClearBits(g_eg_fault, FAULT_REMOTE_STOP);
        break;

#if APP_BRINGUP_MODE
    case CMD_FAULT_INJECT:
        (void)xEventGroupSetBits(g_eg_fault, FAULT_INJECT);
        break;

    case CMD_SET_THROTTLE:              /* 仅调试用 */
        if (f->len >= 2u)
        {
            g_dbg_throttle = (int16_t)((uint16_t)f->payload[0] |
                                       ((uint16_t)f->payload[1] << 8));
        }
        break;

    case CMD_SET_SPEED:                 /* 仅调试用 */
        if (f->len >= 2u)
        {
            g_dbg_speed_x100 = (uint16_t)((uint16_t)f->payload[0] |
                                          ((uint16_t)f->payload[1] << 8));
        }
        break;

    case CMD_DEBUG_INTERLOCK:           /* 仅调试用，专门用来演示 DECEL 状态 */
        if (f->len >= 1u)
        {
            g_dbg_interlock_fail = (f->payload[0] != 0u) ? 1u : 0u;
        }
        break;

#endif
    case CMD_HEARTBEAT:
        break;

    default:
        break;
    }

    App_LogEvent(LOG_EVT_REMOTE_CMD, f->cmd);
}

void CommTask(void *arg)
{
    uint8_t       b;    /* 从环形缓冲里取出的一个字节 */
    proto_frame_t f;    /* 解析器凑出来的一帧；CRC 不对时内容不可信，不会用到 */

    (void)arg;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(PERIOD_COMM_MS));

        while (RingBuf_Get(&s_rb3, &b) != 0)
        {
            if (Protocol_FeedByte(&s_parser3, b, &f) != 0)
            {
                if (seq_accept(&s_filter3, f.seq) != 0)
                {
                    handle_frame(&f);
                }
            }
        }

#if APP_DEBUG_UART1_CMD
        while (RingBuf_Get(&s_rb1, &b) != 0)
        {
            if (Protocol_FeedByte(&s_parser1, b, &f) != 0)
            {
                if (seq_accept(&s_filter1, f.seq) != 0)
                {
                    handle_frame(&f);
                }
            }
        }
#endif
    }
}
