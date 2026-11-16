#include "frame.h"
#include "config.h"

uint8_t Frame_Crc8(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0u; i < 8u; i++)
    {
        crc = (uint8_t)((crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1));
    }
    return crc;
}

size_t Frame_Build(uint8_t *out, size_t cap, uint8_t seq, uint8_t cmd,
                   const uint8_t *payload, uint8_t len)
{
    const size_t total = (size_t)len + 6u;   /* header 2 + seq + cmd + len + crc */
    size_t i;
    uint8_t crc = 0u;

    if ((out == nullptr) || (cap < total) || (len > CFG_FRAME_MAX_PAYLOAD))
    {
        return 0u;
    }
    if ((len > 0u) && (payload == nullptr))
    {
        return 0u;
    }

    out[0] = CFG_FRAME_HDR1;
    out[1] = CFG_FRAME_HDR2;
    out[2] = seq;
    out[3] = cmd;
    out[4] = len;

    crc = Frame_Crc8(crc, seq);
    crc = Frame_Crc8(crc, cmd);
    crc = Frame_Crc8(crc, len);

    for (i = 0u; i < len; i++)
    {
        out[5u + i] = payload[i];
        crc = Frame_Crc8(crc, payload[i]);
    }

    out[5u + len] = crc;
    return total;
}
