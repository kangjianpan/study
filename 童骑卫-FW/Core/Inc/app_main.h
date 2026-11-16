#ifndef __APP_MAIN_H
#define __APP_MAIN_H

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "app_types.h"

/* RTOS 对象
 *
 * 全部在 app_start() 里创建，任何一个创建失败就直接死循环等看门狗。
 * 下面每个对象都注明了：装什么、谁写、谁读。 */

/* 传感器数据队列。元素类型 sensor_data_t，深度 1。
 * 写：SensorTask，每 10 ms 一次 xQueueOverwrite —— 写入即覆盖旧值。
 * 读：ControlTask(5ms)、SafetyTask(10ms)、HmiTask(33ms)、TelemetryTask(100ms)，
 *     全部用 xQueuePeek(...,0) 非阻塞取。
 * 深度刻意做成 1：传感器是"最新值"语义，不是事件流；
 * 读者多一点慢一点都无所谓，谁也不需要历史采样。 */
extern QueueHandle_t      g_q_sensor;

/* 黑匣子事件队列。元素类型 log_event_t，深度 8。
 * 写：任何任务（包括最紧急的 SafetyTask）调用 App_LogEvent()，
 *     非阻塞发送，队列满了这条事件就直接丢掉，绝不堵住调用者。
 * 读：LogTask（优先级最低，1），收到后存进 64 条的 RAM 环形记录并打印。 */
extern QueueHandle_t      g_q_log;

/* 故障事件组。每一位对应 app_types.h 里一个 FAULT_xxx 宏。
 * 写：只有 CommTask —— 远程停车置/清 FAULT_REMOTE_STOP，
 *     故障注入命令置 FAULT_INJECT。
 * 读：SafetyTask（取 FAULT_MASK_REMOTE 合成总故障字）、
 *     HmiTask（打印）、TelemetryTask（塞进遥测帧）。
 * 之所以用事件组而不是队列：这几个读者只关心"此刻有哪些位立着"，
 * 不关心怎么变的，也无所谓错过中间跳变。 */
extern EventGroupHandle_t g_eg_fault;

/* 总线互斥量，保护 I2C1 和 SPI1。
 * 目前还没有任何代码在用 —— drv_imu / drv_tof / drv_nfc 都是桩函数。
 * 等真实驱动接进来后：ICM-42688 走 SPI1(CS=PA4)、MFRC522 走 SPI1(CS=PA2)，
 * 两者共用一根总线，任何一次访问前都必须先拿这把锁；
 * VL53L1X 走 I2C1(PB6/PB7)，也用这一把。
 * 注意：一次 ToF 转换要 20 ms，锁不要跨整次转换一直攥着。 */
extern SemaphoreHandle_t  g_mtx_i2c;

/* 限速配置互斥量，保护 app_main.c 里的 static limit_cfg_t s_limit。
 * 写：CommTask 经 App_SetLimit()（场地/家长/孩子三路限速指令）、
 *     SafetyTask 经 App_SetLinkFallback()（链路断了降速）。
 * 读：ControlTask / SafetyTask / HmiTask / TelemetryTask 经 App_GetLimitSnapshot()。
 * 临界区只有几条赋值，所以三个存取函数取锁超时都定成 5 ms，
 * 抢不到就保留旧值继续跑 —— 绝不为一个限速数字把任务卡住。 */
extern SemaphoreHandle_t  g_mtx_cfg;

/* 调试串口打印互斥量，保护 USART1 上的 printf 输出。
 * 使用者：App_Logf()，以及直接调它的 HmiTask / LogTask。
 * 只保护 printf，不保护 USART3 上的协议帧 ——
 * 那条链路由 TelemetryTask 独占发送，天然只有一个写者，不需要锁。 */
extern SemaphoreHandle_t  g_mtx_uart_log;

/* CommTask 的任务句柄，全工程唯一被保存下来的句柄。
 * 用途：USART1 / USART3 接收中断里调 vTaskNotifyGiveFromISR() 把它直接唤醒，
 * 这样 CommTask 不必空转轮询，也不用到点才去查缓冲区。 */
extern TaskHandle_t       g_comm_task_handle;

/* 链路监视，只由 CommTask 写
 *
 * g_last_link_tick：收到最后一帧有效数据的系统节拍数，handle_frame() 里刷新。
 *   SafetyTask 每 10 ms 拿它和当前节拍相减，超过 LINK_TIMEOUT_MS(3 s)
 *   就判定失联，把限速压到 LIMIT_FALLBACK(6 km/h)。
 * g_link_seen：是否曾经收到过至少一帧。
 *   开机后一直没数据不算失联 —— 否则从没连过的手机
 *   会让车一上电就莫名其妙被限速。只有"先连上、后来断"才降速。 */
extern volatile uint32_t  g_last_link_tick;
extern volatile uint8_t   g_link_seen;

/* 安全结论：此刻是否允许驱动输出。由 SafetyTask 每拍算一次写入，
 * 取值为 App_State_MotorEnabled(当前状态)。
 * 读方：ControlTask（为 0 就把油门清零）、
 *       HmiTask（决定状态灯闪快闪慢）、
 *       TelemetryTask（塞进心跳帧的 bit0）。
 * 用 volatile 不加锁：单字节写是原子的，读者要么看到旧值要么看到新值，
 * 而这里本来就是个"几毫秒前"的结论，没有中间态可言。 */
extern volatile uint8_t   g_drive_allowed;
extern volatile uint32_t  g_control_tick;
extern volatile uint32_t  g_local_faults;

/* 调试覆盖量，由协议命令驱动
 *
 * g_dbg_throttle：油门覆盖值，千分比 0..1000。
 *   -1 = 不覆盖，跟随踏板 ADC。用 -1 而不是 0 当哨兵，
 *   因为 0 是"完全松油门"的合法值，两者必须分得开。
 * g_dbg_speed_x100：车速覆盖值，km/h × 100。0 = 不覆盖。
 *   霍尔还没接，SensorTask 直接把它当 speed_kmh_x100 上报，
 *   没有这一路覆盖就没法在台架上演示超速和碰撞时间逻辑。
 * g_dbg_interlock_fail：1 = 强制认为佩戴传感器失效。
 *   bring-up 模式下真实的联锁检查被 #if 关掉了，
 *   靠这一位才能在桌面上走通 READY -> DECEL 这条路径。 */
extern volatile int16_t   g_dbg_throttle;
extern volatile uint16_t  g_dbg_speed_x100;
extern volatile uint8_t   g_dbg_interlock_fail;

/* 应用启动（由 main.c 调用，永不返回） */
void app_start(void);

/* 限速权限 */
#define LIMIT_SRC_VENUE   0u
#define LIMIT_SRC_PARENT  1u
#define LIMIT_SRC_CHILD   2u
#define LIMIT_SRC_LINK    3u

void App_SetLimit(uint8_t source, uint8_t kmh);
void App_SetLinkFallback(uint8_t kmh);          /* 0 = 链路正常 */
void App_GetLimitSnapshot(limit_cfg_t *out);

/* 各任务的入口函数 */
void SafetyTask(void *arg);
void ControlTask(void *arg);
void SensorTask(void *arg);
void CommTask(void *arg);
void TelemetryTask(void *arg);
void HmiTask(void *arg);
void LogTask(void *arg);

/* 打开串口接收中断。调度器启动之前调用一次。 */
void Comm_RxInit(void);

/* 日志 */
void App_LogEvent(uint8_t code, uint16_t value);
void App_Logf(const char *fmt, ...);
uint16_t App_LogCount(void);
const log_event_t *App_LogGet(uint16_t back);   /* back = 0 取最新一条 */

#endif /* __APP_MAIN_H */
