#include "app_main.h"
#include "app_config.h"
#include "bsp.h"

#include <stdio.h>
#include <string.h>

/* RTOS 对象（详细说明见 app_main.h，这里只标出各自的创建参数）
 *
 * g_q_sensor     深度 1，元素 sensor_data_t —— 只留最新一份采样
 * g_q_log        深度 8，元素 log_event_t  —— 日志事件中转，满了就丢
 * g_eg_fault     事件组，位定义见 app_types.h 的 FAULT_xxx
 * g_mtx_i2c      普通互斥量 —— 保护 I2C1 / SPI1 总线（驱动接进来后才用得上）
 * g_mtx_cfg      普通互斥量 —— 保护下面的 s_limit
 * g_mtx_uart_log 普通互斥量 —— 保护 USART1 上的 printf
 *
 * 全部初始化为 NULL。app_start() 创建后若仍为 NULL 就进入死循环，
 * 等 200 ms 看门狗复位 —— 那种状态下跑下去比不跑更危险。 */
QueueHandle_t      g_q_sensor = NULL;
QueueHandle_t      g_q_log = NULL;
EventGroupHandle_t g_eg_fault = NULL;
SemaphoreHandle_t  g_mtx_i2c = NULL;
SemaphoreHandle_t  g_mtx_cfg = NULL;
SemaphoreHandle_t  g_mtx_uart_log = NULL;

/* CommTask 句柄，唯一目的：让串口接收中断能直接把它叫醒，见 app_main.h。 */
TaskHandle_t       g_comm_task_handle = NULL;

/* 链路活性。g_last_link_tick 由 CommTask 每收到一帧刷新一次，
 * g_link_seen 一旦置 1 就永不清零（表示"这辆车见过了手机"）。
 * SafetyTask 靠这两个判断"连上过但后来断了"这种真正的失联。 */
volatile uint32_t g_last_link_tick = 0u;
volatile uint8_t  g_link_seen = 0u;

/* 安全任务本拍的结论：允许驱动为 1。上电初值 0 —— 在 SafetyTask
 * 跑完第一拍之前，任何人都不能动手，这是最保守的默认。 */
volatile uint8_t  g_drive_allowed = 0u;
volatile uint32_t g_control_tick = 0u;
volatile uint32_t g_local_faults = 0u;

/* 台架调试覆盖量，只能由协议命令 CMD_SET_THROTTLE / CMD_SET_SPEED /
 * CMD_DEBUG_INTERLOCK 改写。初值是有讲究的：
 *   g_dbg_throttle = -1  表示"不覆盖"，跟随真实踏板；
 *   g_dbg_speed_x100 = 0 表示"不覆盖"（霍尔尚未接入，实际等价于车速为 0）；
 *   g_dbg_interlock_fail = 0 表示不伪造联锁故障。 */
volatile int16_t  g_dbg_throttle = -1;
volatile uint16_t g_dbg_speed_x100 = 0u;
volatile uint8_t  g_dbg_interlock_fail = 0u;

/* 限速权限，只通过下面的那几个存取函数访问 ——
 * 因为它是多任务共享的，直接读写会被另一个任务打断。
 * 四个来源取最小值才是最终生效的限速：
 *   venue   场地硬上限，由场馆那侧下发
 *   parent  家长设定
 *   child   孩子端自己选的，只能往下调
 *   fallback 链路断开时临时压上来的值，链路恢复后清零
 * 加进来一个新来源时，记得同步去改 calc_effective()。 */
static limit_cfg_t s_limit;

/* 限速权限的读写接口 */
static uint8_t calc_effective(const limit_cfg_t *l)
{
    uint8_t m = l->venue;

    if (l->parent < m)
    {
        m = l->parent;
    }
    if (l->child < m)
    {
        m = l->child;
    }
    if ((l->fallback > 0u) && (l->fallback < m))
    {
        m = l->fallback;
    }

    return m;
}

void App_SetLimit(uint8_t source, uint8_t kmh)
{
    if (xSemaphoreTake(g_mtx_cfg, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return;
    }

    switch (source)
    {
    case LIMIT_SRC_VENUE:
        s_limit.venue = kmh;
        break;
    case LIMIT_SRC_PARENT:
        s_limit.parent = kmh;
        break;
    case LIMIT_SRC_CHILD:
        /* 孩子端只能往低里调，不许把上限抬高 */
        if (kmh < s_limit.child)
        {
            s_limit.child = kmh;
        }
        break;
    default:
        break;
    }

    s_limit.effective = calc_effective(&s_limit);

    (void)xSemaphoreGive(g_mtx_cfg);

    App_LogEvent(LOG_EVT_LIMIT_CHANGE,
                 (uint16_t)((source << 8) | s_limit.effective));
}

/* 只在链路状态发生变化时由 SafetyTask 调用。传入 0 表示撤销降级。 */
void App_SetLinkFallback(uint8_t kmh)
{
    uint8_t changed;

    if (xSemaphoreTake(g_mtx_cfg, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        return;
    }

    changed = (s_limit.fallback != kmh) ? 1u : 0u;
    s_limit.fallback = kmh;
    s_limit.effective = calc_effective(&s_limit);

    (void)xSemaphoreGive(g_mtx_cfg);

    if (changed != 0u)
    {
        App_LogEvent(LOG_EVT_LIMIT_CHANGE,
                     (uint16_t)((LIMIT_SRC_LINK << 8) | s_limit.effective));
    }
}

void App_GetLimitSnapshot(limit_cfg_t *out)
{
    if (xSemaphoreTake(g_mtx_cfg, pdMS_TO_TICKS(5)) != pdTRUE)
    {
        out->venue = 0u;
        out->parent = 0u;
        out->child = 0u;
        out->fallback = 0u;
        out->effective = 0u;
        return; /* Fail closed rather than leave an uninitialized/old limit. */
    }

    *out = s_limit;

    (void)xSemaphoreGive(g_mtx_cfg);
}

/* 创建 7 个任务
 *
 * 优先级从高到低：safety(6) > control(5) > sensor(4) > comm(3)
 *               > telemetry(2) = hmi(2) > log(1)
 * telemetry 和 hmi 同优先级，靠时间片轮转，谁都不会饿死。
 * 只有 comm 需要保存句柄，因为串口中断要直接唤醒它。 */
static void create_tasks(void)
{
    /* 10 ms 扫一遍故障、跑状态机、喂狗。最高优先级，谁也别想挡它。 */
    xTaskCreate(SafetyTask,  "safety",  STK_SAFETY,  NULL, PRIO_SAFETY,  NULL);
    /* 5 ms 一次，把踏板/限速/避障/斜率算完丢给电调。 */
    xTaskCreate(ControlTask, "control", STK_CONTROL, NULL, PRIO_CONTROL, NULL);
    /* 10 ms 采一次 IMU/ADC，30 ms 采 ToF，50 ms 采 NFC。 */
    xTaskCreate(SensorTask,  "sensor",  STK_SENSOR,  NULL, PRIO_SENSOR,  NULL);
    /* 收帧、拆帧、执行命令。无数据时被通知阻塞，不占 CPU。 */
    xTaskCreate(CommTask,    "comm",    STK_COMM,    NULL, PRIO_COMM,    &g_comm_task_handle);
    /* 100 ms 一帧遥测，每 1 s 一帧心跳。走 USART3 给 ESP32。 */
    xTaskCreate(TelemetryTask,   "cloud",   STK_TELEMETRY,   NULL, PRIO_TELEMETRY,   NULL);
    /* 33 ms 一次：状态灯闪烁 + 每秒一行调试打印。 */
    xTaskCreate(HmiTask,     "hmi",     STK_HMI,     NULL, PRIO_HMI,     NULL);
    /* 最低优先级：从日志队列里取事件、存黑匣子、打印。
     * 只有别的任务都闲着它才能跑，绝不会拖慢控制回路。 */
    xTaskCreate(LogTask,     "log",     STK_LOG,     NULL, PRIO_LOG,     NULL);
}

/* FreeRTOS 钩子函数
 *
 * 两个都是先打一行 ASCII 标记，然后彻底停住。
 * 停在这里是故意的：SafetyTask 从此不会再喂狗，
 * 200 ms 后硬件看门狗复位，而不是让卡丁车在没人管的情况下继续跑。
 * （IWDG 靠 LSI 独立计数，中断全关也拦不住它。） */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;

    /* configCHECK_FOR_STACK_OVERFLOW == 2 时内核会把任务名带过来，
     * 这里的判空只是防御性写法。
     * 注意本函数是在上下文切换里被调用的（异常上下文），
     * 打印走的是阻塞式轮询串口，会有毫秒级延迟，只因为反正是要死了才容忍。 */
    printf("\r\nFATAL: stack overflow in %s\r\n",
           (pcTaskName != NULL) ? pcTaskName : "?");

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void vApplicationMallocFailedHook(void)
{
    printf("\r\nFATAL: heap exhausted\r\n");

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

/* 应用入口 */
void app_start(void)
{
    sensor_data_t zero;     /* 占位样本，清零后先塞进队列 */
    uint8_t       reason;   /* Bsp_GetResetReason() 的返回值，只用于开机横幅 */

    memset(&zero, 0, sizeof(zero));

    /* 六个内核对象的创建。参数在这里能一眼看出"多大、装什么"：
     * 传感器队列深度 1（只要最新值），日志队列深度 8（能扛一小段突发）。
     * 三个信号量都建成互斥量而不是二值信号量：
     * 互斥量带优先级继承，持锁的低优先级任务被抢占时不会把高优先级任务拖住。 */
    g_q_sensor     = xQueueCreate(1, sizeof(sensor_data_t));
    g_q_log        = xQueueCreate(8, sizeof(log_event_t));
    g_eg_fault     = xEventGroupCreate();
    g_mtx_i2c      = xSemaphoreCreateMutex();
    g_mtx_cfg      = xSemaphoreCreateMutex();
    g_mtx_uart_log = xSemaphoreCreateMutex();

    /* 注意：堆真不够时，pvPortMalloc 内部会先调 vApplicationMallocFailedHook()，
     * 在那里死循环并被看门狗复位，所以下面这条分支其实走不到。
     * 留着是为了将来万一关掉 configUSE_MALLOC_FAILED_HOOK 还能兜底。 */
    if ((g_q_sensor == NULL) || (g_q_log == NULL) || (g_eg_fault == NULL) ||
        (g_mtx_i2c == NULL) || (g_mtx_cfg == NULL) || (g_mtx_uart_log == NULL))
    {
        printf("FATAL: RTOS object creation failed\r\n");
        for (;;)
        {
        }
    }

    /* 先塞一份全零样本，免得消费者 Peek 到一个空队列 */
    (void)xQueueOverwrite(g_q_sensor, &zero);

    s_limit.venue     = LIMIT_VENUE_DEFAULT;
    s_limit.parent    = LIMIT_PARENT_DEFAULT;
    s_limit.child     = LIMIT_CHILD_DEFAULT;
    s_limit.effective = calc_effective(&s_limit);

    /* 读一下复位来源，好区分"被看门狗拉回来的"和"正常上电" */
    reason = Bsp_GetResetReason();

    printf("\r\n");
    printf("============================================\r\n");
    printf(" GuardKart %s\r\n", APP_VERSION_STR);
    if (reason == RESET_IWDG)
    {
        printf(" RESET: watchdog - supervisor intervened\r\n");
    }
    else if (reason == RESET_PIN)
    {
        printf(" RESET: power-on / reset pin\r\n");
    }
    else
    {
        printf(" RESET: unknown\r\n");
    }
#if APP_BRINGUP_MODE
    printf(" INTERLOCK: bypassed (bring-up mode)\r\n");
#else
    printf(" INTERLOCK: enforced\r\n");
#endif
    printf(" limit: venue=%u parent=%u child=%u -> %u km/h\r\n",
           (unsigned)s_limit.venue, (unsigned)s_limit.parent,
           (unsigned)s_limit.child, (unsigned)s_limit.effective);
    printf("============================================\r\n");

    Bsp_ClearResetFlags();

    Comm_RxInit();

    create_tasks();

    printf(" heap free after tasks: %u bytes\r\n",
           (unsigned)xPortGetFreeHeapSize());
    printf(" starting scheduler\r\n");

    vTaskStartScheduler();

    /* 只有堆内存不够、调度器起不来时才会走到这里 */
    printf("FATAL: scheduler returned\r\n");
    for (;;)
    {
    }
}
