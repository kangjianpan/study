#include "protocol.h"

/* 解析器状态 */
#define PARSER_SOF1     0u
#define PARSER_SOF2     1u
#define PARSER_SEQ      2u
#define PARSER_CMD      3u
#define PARSER_LEN      4u
#define PARSER_PAYLOAD  5u
#define PARSER_CRC      6u

/* 增量式 CRC8，多项式 0x07。
 * 每收一个字节就往前推一次，不用先把整帧缓存下来再算。 */
static void crc8_update(uint8_t *crc, uint8_t byte)
{
    uint8_t b;

    *crc ^= byte;
    for (b = 0u; b < 8u; b++)
    {
        if ((*crc & 0x80u) != 0u)
        {
            *crc = (uint8_t)((*crc << 1) ^ 0x07u);
        }
        else
        {
            *crc = (uint8_t)(*crc << 1);
        }
    }
}

uint8_t Protocol_Crc8(const uint8_t *data, uint16_t len)
{
    uint8_t  crc = 0x00u;
    uint16_t i;

    if (data == 0)
    {
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        crc8_update(&crc, data[i]);
    }

    return crc;
}

uint16_t Protocol_Build(uint8_t *out, uint16_t out_size,
                        uint8_t seq, uint8_t cmd,
                        const uint8_t *payload, uint8_t len)
{
    uint16_t n = 0u;
    uint8_t  crc = 0x00u;
    uint8_t  i;

    if (out == 0)
    {
        return 0u;
    }
    if (len > PROTO_MAX_PAYLOAD)
    {
        return 0u;
    }
    if (out_size < (uint16_t)(PROTO_OVERHEAD + len))
    {
        return 0u;
    }

    out[n++] = PROTO_SOF1;
    out[n++] = PROTO_SOF2;
    out[n++] = seq;
    out[n++] = cmd;
    out[n++] = len;

    crc8_update(&crc, seq);
    crc8_update(&crc, cmd);
    crc8_update(&crc, len);

    for (i = 0u; i < len; i++)
    {
        out[n++] = payload[i];
        crc8_update(&crc, payload[i]);
    }

    out[n++] = crc;

    return n;
}

void Protocol_Init(proto_parser_t *p)
{
    uint8_t i;

    p->state = PARSER_SOF1;
    p->seq   = 0u;
    p->cmd   = 0u;
    p->len   = 0u;
    p->idx   = 0u;
    p->crc   = 0x00u;

    for (i = 0u; i < PROTO_MAX_PAYLOAD; i++)
    {
        p->payload[i] = 0u;
    }
}

int Protocol_FeedByte(proto_parser_t *p, uint8_t byte, proto_frame_t *frame)
{
    switch (p->state)
    {
    case PARSER_SOF1:
        if (byte == PROTO_SOF1)
        {
            p->state = PARSER_SOF2;
        }
        break;

    case PARSER_SOF2:
        if (byte == PROTO_SOF2)
        {
            p->state = PARSER_SEQ;
        }
        else if (byte != PROTO_SOF1)
        {
            /* 收到的是 0x55 以外的字节。
             * 只有它恰好又是 0xAA 时才留在本状态继续等 0x55
             * （说明上一帧是误对齐的假帧头），否则退回帧头搜索。 */
            p->state = PARSER_SOF1;
        }
        break;

    case PARSER_SEQ:
        p->seq   = byte;
        p->crc   = 0x00u;
        crc8_update(&p->crc, byte);
        p->state = PARSER_CMD;
        break;

    case PARSER_CMD:
        p->cmd = byte;
        crc8_update(&p->crc, byte);
        p->state = PARSER_LEN;
        break;

    case PARSER_LEN:
        if (byte > PROTO_MAX_PAYLOAD)
        {
            /* 长度字段超限：说明帧已经错位了，丢掉重来，绝不去读越界的 payload */
            Protocol_Init(p);
            break;
        }
        p->len   = byte;
        p->idx   = 0u;
        crc8_update(&p->crc, byte);
        p->state = (byte == 0u) ? PARSER_CRC : PARSER_PAYLOAD;
        break;

    case PARSER_PAYLOAD:
        p->payload[p->idx] = byte;
        p->idx++;
        crc8_update(&p->crc, byte);
        if (p->idx >= p->len)
        {
            p->state = PARSER_CRC;
        }
        break;

    case PARSER_CRC:
    default:
        /* CRC 对得上才把帧交出去；不管对没对上都要复位解析器，
         * 免得这一帧坏了把下一帧也带偏 */
        if ((byte == p->crc) && (frame != 0))
        {
            uint8_t i;

            frame->seq = p->seq;
            frame->cmd = p->cmd;
            frame->len = p->len;
            for (i = 0u; i < p->len; i++)
            {
                frame->payload[i] = p->payload[i];
            }
            Protocol_Init(p);
            return 1;
        }
        Protocol_Init(p);
        break;
    }

    return 0;
}
