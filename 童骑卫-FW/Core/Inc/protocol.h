#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#include <stdint.h>
#include "app_config.h"

/* STM32 与 ESP32 共用的串口帧格式
 *
 *   AA 55 | SEQ | CMD | LEN | PAYLOAD | CRC8
 *
 * CRC8：多项式 0x07，初值 0x00，覆盖范围从 SEQ 到 PAYLOAD
 *
 * 每条物理链路各持有自己的解析器实例，
 * 这样调试串口和 ESP32 串口上的数据交错到达也不会串帧。 */

#define PROTO_SOF1              0xAAu
#define PROTO_SOF2              0x55u
#define PROTO_HEADER_LEN        5u
#define PROTO_OVERHEAD          6u
#define PROTO_MAX_FRAME         (PROTO_OVERHEAD + PROTO_MAX_PAYLOAD)

/* 命令字 */
#define CMD_TELEMETRY           0x10u
#define CMD_SET_LIMIT           0x20u
#define CMD_STOP                0x21u
#define CMD_RESUME              0x22u
#define CMD_SET_LIMIT_VENUE     0x23u
#define CMD_SET_LIMIT_CHILD     0x24u
#define CMD_SET_THROTTLE        0x25u     /* 仅调试用 */
#define CMD_SET_SPEED           0x26u     /* 仅调试用 */
#define CMD_DEBUG_INTERLOCK     0x27u     /* 仅调试用：1 = 强制佩戴失效 */
#define CMD_HEARTBEAT           0x30u
#define CMD_FAULT_INJECT        0x40u

/* 增量式解析器的状态。每条链路各持一份实例——
 * 半帧状态是全局的隐患，两个链路共用就会互相把对方的帧吃掉。 */
typedef struct
{
    uint8_t state;      /* 当前停在哪一步，取值 PARSER_SOF1 .. PARSER_CRC */
    uint8_t seq;        /* 本帧的序号字段 */
    uint8_t cmd;        /* 本帧的命令字 */
    uint8_t len;        /* 本帧载荷长度 */
    uint8_t idx;        /* 载荷已收字节数 */
    uint8_t crc;        /* 边收边算的 CRC8，收到最后一位时和帧里的 CRC 比对 */
    uint8_t payload[PROTO_MAX_PAYLOAD]; /* 载荷缓冲，最大 32 字节 */
} proto_parser_t;

/* 解析成功后交出的一帧。解析器内部还会继续用它自己的缓冲，
 * 所以这里必须把需要的内容拷出来，不能只给指针。 */
typedef struct
{
    uint8_t seq;        /* 已通过 CRC 校验的帧序号 */
    uint8_t cmd;        /* 命令字，见 CMD_xxx */
    uint8_t len;        /* 载荷实际长度 */
    uint8_t payload[PROTO_MAX_PAYLOAD];
} proto_frame_t;

uint8_t  Protocol_Crc8(const uint8_t *data, uint16_t len);

/* 返回写入的总字节数，出错返回 0 */
uint16_t Protocol_Build(uint8_t *out, uint16_t out_size,
                        uint8_t seq, uint8_t cmd,
                        const uint8_t *payload, uint8_t len);

void     Protocol_Init(proto_parser_t *p);

/* 喂入一个字节。凑出完整且 CRC 正确的帧时返回 1。 */
int      Protocol_FeedByte(proto_parser_t *p, uint8_t byte, proto_frame_t *frame);

#endif /* __PROTOCOL_H */
