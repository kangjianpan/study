#include "stm32_link.h"

void Stm32Link::begin(void)
{
    uart_.begin(CFG_STM32_BAUD, SERIAL_8N1, CFG_STM32_RX_PIN, CFG_STM32_TX_PIN);
}

void Stm32Link::poll(void)
{
    uint16_t n = 0u;

    while ((n < CFG_DRAIN_MAX_BYTES) && (uart_.available() > 0))
    {
        const int c = uart_.read();
        if (c < 0)
        {
            break;
        }
        feed((uint8_t)c);
        n++;
    }
}

void Stm32Link::feed(uint8_t b)
{
    switch (st_)
    {
    case S_H1:
        if (b == CFG_FRAME_HDR1)
        {
            st_ = S_H2;
        }
        break;

    case S_H2:
        st_ = (b == CFG_FRAME_HDR2) ? S_SEQ : S_H1;
        break;

    case S_SEQ:
        seq_  = b;
        crc_  = Frame_Crc8(0u, b);
        st_   = S_CMD;
        break;

    case S_CMD:
        cmd_  = b;
        crc_  = Frame_Crc8(crc_, b);
        st_   = S_LEN;
        break;

    case S_LEN:
        len_  = b;
        crc_  = Frame_Crc8(crc_, b);
        idx_  = 0u;
        if (len_ > CFG_FRAME_MAX_PAYLOAD)
        {
            st_ = S_H1;                       /* implausible length: resync */
        }
        else
        {
            st_ = (len_ > 0u) ? S_PAY : S_CRC;
        }
        break;

    case S_PAY:
        buf_[idx_++] = b;
        crc_ = Frame_Crc8(crc_, b);
        if (idx_ >= len_)
        {
            st_ = S_CRC;
        }
        break;

    case S_CRC:
        if (b == crc_)
        {
            commit();
        }
        else
        {
            crc_errors_++;
        }
        st_ = S_H1;
        break;

    default:
        st_ = S_H1;
        break;
    }
}

void Stm32Link::commit(void)
{
    /* Depth 4 is plenty at 10 Hz. If the consumer ever falls behind, the
     * newest frame is the one that matters, so the oldest is dropped. */
    if ((uint8_t)((q_head_ + 1u) % 4u) == q_tail_)
    {
        q_tail_ = (uint8_t)((q_tail_ + 1u) % 4u);
    }

    q_[q_head_].cmd = cmd_;
    q_[q_head_].len = len_;
    for (uint8_t i = 0u; i < len_; i++)
    {
        q_[q_head_].payload[i] = buf_[i];
    }
    q_head_ = (uint8_t)((q_head_ + 1u) % 4u);

    last_rx_ms_ = millis();
    rx_frames_++;
}

bool Stm32Link::nextFrame(RxFrame *out)
{
    if ((out == nullptr) || (q_head_ == q_tail_))
    {
        return false;
    }

    *out = q_[q_tail_];
    q_tail_ = (uint8_t)((q_tail_ + 1u) % 4u);
    return true;
}

bool Stm32Link::send(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[CFG_FRAME_MAX_PAYLOAD + 6u];
    const size_t n = Frame_Build(frame, sizeof(frame), tx_seq_, cmd, payload, len);

    if (n == 0u)
    {
        return false;
    }

    uart_.write(frame, n);
    tx_seq_++;
    return true;
}
