#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

#include <stdint.h>

/* GuardKart 中央配置文件
 *
 * 所有可调参数都集中在这里，不要在各模块里散落魔法数字。
 * 中文只出现在注释里；printf 的格式串一律保持纯 ASCII，
 * 否则 ARMCC 会报 #8: missing closing quote。 */

#define APP_VERSION_STR         "0.2.0-hw-v2"

/* 编译开关 */
/* 1 = 台架调试模式：IMU / ToF / NFC 还没装，
 *     佩戴连锁整个旁路掉，好在桌面上先跑起来。
 *     传感器接好并标定之后改回 0。 */
#define APP_BRINGUP_MODE        0

/* 1 = 打开独立看门狗。用调试器单步时记得改成 0，
 *     否则每停在一个断点上芯片就会被复位。 */
#define APP_USE_IWDG            1

/* 1 = 除 USART3 外，调试串口 USART1 也接受协议帧。
 *     这样只插一根 USB 线就能发指令测试。 */
#define APP_DEBUG_UART1_CMD     0

/* 任务优先级（configMAX_PRIORITIES = 32，这里只用到 1..6，数字越大越优先） */
#define PRIO_SAFETY             6
#define PRIO_CONTROL            5
#define PRIO_SENSOR             4
#define PRIO_COMM               3
#define PRIO_TELEMETRY              2
#define PRIO_HMI                2
#define PRIO_LOG                1

/* 任务栈大小，单位是字（1 字 = 4 字节），所以 128 就是 512 字节 */
#define STK_SAFETY              128
#define STK_CONTROL             192
#define STK_SENSOR              192
#define STK_COMM                256
#define STK_TELEMETRY               256
#define STK_HMI                 192
#define STK_LOG                 256

/* 各任务的周期，单位毫秒 */
#define PERIOD_SAFETY_MS        10
#define PERIOD_CONTROL_MS       5
#define PERIOD_SENSOR_MS        10
#define PERIOD_COMM_MS          20
#define PERIOD_TELEMETRY_MS         100
#define PERIOD_HMI_MS           33

/* 安全阈值 */
/* 状态机（对应方案文档 7.5 节） */
#define SELFTEST_MS             200     /* 复位后等传感器稳定的时间 */
#define DECEL_TIMEOUT_MS        3000    /* DECEL 超过这么久没恢复就锁死 */

/* 踏板门限，单位千分比。进 RUN 要求明确踩下，回 READY 要求明确松开；
 * 两者之间留这段死区，是为了防止"油门本来就踩着"的时候一使能就直接窜出去。 */
#define PEDAL_RUN_THRESHOLD     100
#define PEDAL_IDLE_THRESHOLD    50

/* 0 = 踏板还没接：SensorTask 一律上报 0，
 *     免得悬空的 PA3 上那点 ADC 噪声把状态机一路推到 RUN。
 *     分压电路焊好并标定之后改 1。 */
#define APP_PEDAL_PRESENT       1

/* 1 = 进 READY 就拉高电调使能线。航模电调上电后要先收到一段中立脉宽才会上膛，
 *     所以必须提前给使能，否则到了 RUN 它还是没准备的状态。
 * 0 = 只在 RUN / DECEL 使能。这样孩子不踩踏板车子就永远上不了膛，
 *     第一次 RUN 是从"未上膛"开始的。
 *
 * 注意：READY 到底算不算安全的待机，完全取决于电调是否真把 1500 us 当成零扭矩。
 * 这一点必须在实物电调上实测过，才能在设计文档里写"READY 是安全待机状态"，
 * 见方案文档 13.3 节。 */
#define APP_ENABLE_IN_READY     1

#define LIMIT_VENUE_DEFAULT     12      /* km/h，场地硬上限 */
#define LIMIT_PARENT_DEFAULT    10      /* km/h，家长设定 */
#define LIMIT_CHILD_DEFAULT     12      /* km/h，孩子端选择，只许往下调 */
#define LIMIT_FALLBACK          6       /* km/h，手机断连后降到的速度 */

#define LINK_TIMEOUT_MS         3000    /* BLE / 串口心跳超时 */
#define INTERLOCK_DEBOUNCE_MS   300     /* 佩戴传感器要稳定这么久才算数 */
#define ROLL_LIMIT_DEG10        450     /* 侧翻角门限，45.0 度 */
#define ROLL_TIME_MS            200     /* 角度要持续这么久才判定侧翻 */
#define CRASH_ACCEL_G10         30      /* 碰撞加速度门限，3.0 g */
#define TTC_WARN_MS             1500
#define TTC_BRAKE_MS            800
#define TOF_STOP_MM             300

/* 柔性限速 */
#define SOFT_LIMIT_DEADBAND_X100  50    /* 超出限速 0.5 km/h 就开始收油门 */
#define OVERSPEED_FACTOR_X100    120    /* 到限速的 1.20 倍算硬故障 */

/* 油门斜率，单位千分比 / 每个控制周期（5 ms） */
#define THROTTLE_RAMP_UP        25
#define THROTTLE_RAMP_DOWN      60

/* 电机 / 电调 */
#define ESC_PULSE_NEUTRAL_US    1500
#define ESC_PULSE_MIN_US        1000
#define ESC_PULSE_MAX_US        2000

/* ADC 换算 */
#define ADC_VREF_MV             3300U
#define ADC_FULL_SCALE          4095U
#define VBUS_DIVIDER_X10        190 /* 180k / 10k, ratio 19 */

/* 电流采样通道。
 * ACS712 供电 5 V、输出摆幅 0..5 V，直接进 MCU 会顶到钳位二极管，
 * 所以 J16-3 先过 R28 10k / R29 15k 分压：5 V × 0.6 = 3.0 V，正落在 ADC 量程内。
 * （若直连 R13，超过 3.6 V 的部分全被钳位管吃掉，-20 A 型号在 8 A 就读满了。）
 * 该器件是双向输出，零电流时停在 2.5 V，所以换算前要先扣掉这个零点。
 * CURR_MV_PER_AMP：66 对应 -30 A 型号，100 对应 -20 A 型号。
 * 建议用 -30 A 那颗：-20 A 上，25 A 过流门限已经落在量程的最后一个码值上。 */
#define CURR_DIVIDER_X10        6       /* 10k / 15k 分压，比例 0.6 */
#define CURR_ZERO_MV            2500    /* 零电流时的静态输出（传感器侧） */
#define CURR_MV_PER_AMP         66
#define CURR_ZERO_COUNT         ((uint16_t)((uint32_t)CURR_ZERO_MV *              \
                                            CURR_DIVIDER_X10 / 10u *              \
                                            ADC_FULL_SCALE / ADC_VREF_MV))
#define CURRENT_MA_PER_COUNT    ((ADC_VREF_MV * 10000UL) /                        \
                                 (ADC_FULL_SCALE * CURR_DIVIDER_X10 * CURR_MV_PER_AMP))

/* Hardware v2 enables fault checks. Drive stays disabled until commissioning. */
#define APP_CHECK_VBUS          1
#define APP_CHECK_OVERCURRENT   1
#define UNDERVOLT_MV            22400U /* 7S: 3.2V/cell, measures protected VBAT */
#define OVERCURRENT_MA          25000U

/* 协议 / 链路 */
#define PROTO_MAX_PAYLOAD       32
#define PROTO_TELEMETRY_LEN     14
#define LINK_HB_PERIOD_MS      1000

/* 黑匣子 */
#define LOGBOX_DEPTH            64      /* 在 RAM 里保留的事件条数 */

/* Hardware v2: no vehicle drive until real sensor drivers and commissioning pass.
 * This is deliberately separate from the debug/interlock bypass switch. */
#define APP_DRIVE_COMMISSIONED   0
#define SENSOR_STALE_MS          100U
#define CONTROL_STALE_MS         50U
#define PEDAL_IDLE_MV            800U
#define PEDAL_FULL_MV            4200U
#define PEDAL_FAULT_LOW_MV       500U
#define PEDAL_FAULT_HIGH_MV      4500U

#endif /* __APP_CONFIG_H */
