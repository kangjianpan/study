#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include <stdint.h>

/* 公共数据类型 */

/* SensorTask 产出的原始测量值，由 ControlTask / SafetyTask 消费 */
typedef struct
{
    uint16_t speed_kmh_x100;        /* 车速，12.34 km/h 记作 1234 */
    int16_t  accel_mg[3];           /* 加速度，单位毫 g，x/y/z */
    int16_t  gyro_dps10[3];         /* 角速度，单位 0.1 度/秒，x/y/z */
    uint16_t tof_mm[2];             /* 前左 / 前右 测距 */
    uint16_t current_ma;
    uint16_t bus_mv;
    uint16_t throttle_permille;     /* 踏板 ADC 换算出的千分比，0..1000 */
    uint8_t  seat_pressed;
    uint8_t  belt_closed;
    uint8_t  helmet_ok;
    uint8_t  imu_present;
    uint8_t  tof_present;
    uint8_t  nfc_present;
    uint32_t sample_tick;
    uint8_t  sample_valid;
    uint8_t  pedal_fault;
    uint8_t  speed_valid;
    uint8_t  seat_left_pressed;
    uint8_t  seat_right_pressed;
    uint16_t hall_pulses;
} sensor_data_t;

/* 限速权限。effective = min(venue, parent, child, fallback)。
 * 链路正常时 fallback 为 0；手机那头没动静了就置成 LIMIT_FALLBACK，
 * 于是断连的结果是"车变慢"，而不是"完全没有上限"。 */
typedef struct
{
    uint8_t venue;
    uint8_t parent;
    uint8_t child;
    uint8_t fallback;
    uint8_t effective;
} limit_cfg_t;

/* 14 字节遥测，和方案文档 6.2 节的字段逐字节对齐 */
typedef struct
{
    uint16_t speed_kmh_x100;
    uint8_t  limit_kmh;
    uint8_t  fault_bits;
    uint16_t lap_ms_x10;
    uint8_t  battery_v10;
    uint8_t  wear_state;
    uint16_t trip_m;
} telemetry_t;

/* 黑匣子事件 */
typedef struct
{
    uint32_t tick_ms;
    uint8_t  code;
    uint16_t value;
    uint8_t  reserved;
} log_event_t;

/* 故障位，同时镜像进 FreeRTOS 事件组 */
#define FAULT_ROLLOVER          (1u << 0)   /* 侧翻 */
#define FAULT_CRASH             (1u << 1)   /* 碰撞 */
#define FAULT_OVERCURRENT       (1u << 2)   /* 过流 */
#define FAULT_INTERLOCK         (1u << 3)   /* 佩戴连锁失效 */
#define FAULT_LINK_LOST         (1u << 4)   /* 与手机 / 网关失联 */
#define FAULT_OVERSPEED         (1u << 5)   /* 超速 */
#define FAULT_UNDERVOLT         (1u << 6)   /* 电池欠压 */
#define FAULT_ESTOP             (1u << 7)   /* 急停按下 */
#define FAULT_REMOTE_STOP       (1u << 8)   /* 远程停车指令 */
#define FAULT_INJECT            (1u << 9)   /* 故障注入，演示用 */

#define FAULT_SENSOR            (1u << 10)
#define FAULT_PEDAL             (1u << 11)
#define FAULT_COMMISSION        (1u << 12)
#define FAULT_MASK_ALL          (0x1FFFu)
#define FAULT_MASK_REMOTE       (FAULT_REMOTE_STOP | FAULT_INJECT)

/* 日志事件码 */
#define LOG_EVT_BOOT            0x01        /* 开机 */
#define LOG_EVT_RESET_IWDG      0x02        /* 看门狗复位 */
#define LOG_EVT_RESET_PIN       0x03        /* 复位引脚 / 上电 */
#define LOG_EVT_FAULT_SET       0x10        /* 故障置位 */
#define LOG_EVT_FAULT_CLEAR     0x11        /* 故障清除 */
#define LOG_EVT_LIMIT_CHANGE    0x20        /* 限速变更 */
#define LOG_EVT_REMOTE_CMD      0x30        /* 收到远程指令 */
#define LOG_EVT_STATUS          0x40        /* 周期状态快照 */
#define LOG_EVT_STATE           0x50        /* 状态切换 */

/* 佩戴状态位域，打包进遥测的第 7 字节 */
#define WEAR_SEAT_SHIFT         0           /* 座位压力片 */
#define WEAR_BELT_SHIFT         2           /* 安全带 */
#define WEAR_HELMET_SHIFT       4           /* 头盔 */

#endif /* __APP_TYPES_H */
