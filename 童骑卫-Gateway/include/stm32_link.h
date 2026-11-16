#pragma once

#include <Arduino.h>
#include "config.h"
#include "frame.h"

/*  Owner of UART1, the link to the STM32.
 *
 *  Every method here is non-blocking. poll() reads at most
 *  CFG_DRAIN_MAX_BYTES per call so a flood of frames can never starve the
 *  rest of the system; the caller decides how often to call it.
 *
 *  Rule carried over from the design doc (3.2): this object is the only
 *  thing allowed to touch UART1, and it is driven by the highest priority
 *  application task.  */

struct RxFrame
{
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[CFG_FRAME_MAX_PAYLOAD];
};

class Stm32Link
{
public:
    void begin(void);

    /*  Reads whatever the UART has, up to the per-call budget.  */
    void poll(void);

    /*  Pops one complete, CRC-checked frame.  Returns false when empty.  */
    bool nextFrame(RxFrame *out);

    /*  Builds and writes a frame.  Only one task may call this.  */
    bool send(uint8_t cmd, const uint8_t *payload, uint8_t len);

    uint32_t lastRxMs(void) const { return last_rx_ms_; }
    uint32_t rxFrames(void) const { return rx_frames_; }
    uint32_t crcErrors(void) const { return crc_errors_; }

private:
    void feed(uint8_t b);
    void commit(void);

    HardwareSerial uart_{CFG_STM32_UART_NUM};

    /* parser */
    enum State : uint8_t { S_H1, S_H2, S_SEQ, S_CMD, S_LEN, S_PAY, S_CRC };
    State    st_ = S_H1;
    uint8_t  seq_ = 0u;
    uint8_t  cmd_ = 0u;
    uint8_t  len_ = 0u;
    uint8_t  idx_ = 0u;
    uint8_t  crc_ = 0u;
    uint8_t  buf_[CFG_FRAME_MAX_PAYLOAD] = {0};

    /* completed frames, depth 4 */
    RxFrame  q_[4];
    uint8_t  q_head_ = 0u;
    uint8_t  q_tail_ = 0u;

    uint8_t  tx_seq_ = 0u;
    uint32_t last_rx_ms_ = 0u;
    uint32_t rx_frames_ = 0u;
    uint32_t crc_errors_ = 0u;
};
