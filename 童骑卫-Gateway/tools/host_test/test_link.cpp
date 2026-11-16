/*  Host-side regression test for the STM32 frame parser.
 *
 *  Build and run (from the project root, Git Bash or WSL):
 *
 *    g++ -std=gnu++11 -DARDUINO_USB_CDC_ON_BOOT=1 \
 *        -Itools/host_test -Iinclude \
 *        src/frame.cpp src/telemetry.cpp src/stm32_link.cpp \
 *        tools/host_test/test_link.cpp -o /tmp/gk_test && /tmp/gk_test
 *
 *  No hardware needed. The expected CRC bytes below come from the STM32
 *  README (section 4.1), so a green run also proves the two ends agree on
 *  the wire format -- which is the thing that silently breaks when someone
 *  edits protocol.c.  */

#include <stdio.h>
#include <string.h>

#include "stm32_link.h"
#include "telemetry.h"

uint8_t __stub_rx[1024];
int     __stub_rx_n = 0;
uint8_t __stub_tx[1024];
int     __stub_tx_n = 0;

SerialStub    Serial;
unsigned long millis(void) { return 0u; }
void          delay(unsigned long) {}

static int fails = 0;

static void check(const char *name, bool cond)
{
    printf("%-42s %s\n", name, cond ? "ok" : "FAIL");
    if (!cond)
    {
        fails++;
    }
}

static void push(const uint8_t *p, size_t n)
{
    memcpy(__stub_rx + __stub_rx_n, p, n);
    __stub_rx_n += (int)n;
}

int main(void)
{
    static Stm32Link link;
    RxFrame f;

    link.begin();

    /* 1. frame bytes exactly as documented in the STM32 README */
    const uint8_t hb[] = {0xAA, 0x55, 0x01, 0x30, 0x00, 0x92};
    push(hb, sizeof(hb));
    link.poll();
    check("parse one heartbeat", link.nextFrame(&f) && (f.cmd == 0x30) && (f.len == 0u));

    /* 2. two frames in one poll must stay in order */
    uint8_t lim[7];
    Frame_Build(lim, sizeof(lim), 0x02, CMD_PARENT_LIMIT, (const uint8_t *)"\x08", 1u);
    push(hb, sizeof(hb));
    push(lim, sizeof(lim));
    link.poll();
    const bool a = link.nextFrame(&f) && (f.cmd == 0x30);
    const bool b = link.nextFrame(&f) && (f.cmd == 0x20) && (f.len == 1u) &&
                   (f.payload[0] == 8u);
    check("two frames in one poll, in order", a && b);

    /* 3. bad CRC is dropped and counted, nothing is delivered */
    const uint32_t e0 = link.crcErrors();
    const uint8_t  bad[] = {0xAA, 0x55, 0x01, 0x30, 0x00, 0x93};
    push(bad, sizeof(bad));
    link.poll();
    check("bad CRC dropped", (!link.nextFrame(&f)) && (link.crcErrors() == e0 + 1u));

    /* 4. garbage before a header. A truncated frame can swallow the next
     *    header, so recovery is checked over two frames, not one. */
    const uint8_t junk[] = {0x00, 0xAA, 0x11, 0xAA, 0x55, 0xAA};
    push(junk, sizeof(junk));
    push(hb, sizeof(hb));
    push(hb, sizeof(hb));
    link.poll();
    check("resync after garbage", link.nextFrame(&f) && (f.cmd == 0x30));

    /* 5. an implausible LEN must not overrun the payload buffer */
    const uint8_t biglen[] = {0xAA, 0x55, 0x01, 0x30, 0x40};
    push(biglen, sizeof(biglen));
    push(hb, sizeof(hb));
    link.poll();
    check("LEN>32 resyncs", link.nextFrame(&f) && (f.cmd == 0x30));

    /* 6. a full telemetry payload arrives byte for byte */
    uint8_t tm[6 + 14 + 1];
    const size_t n = Frame_Build(tm, sizeof(tm), 7u, CMD_TELEMETRY,
                                 (const uint8_t *)"ABCDEFGHIJKLMN", 14u);
    push(tm, n);
    link.poll();
    check("14-byte telemetry payload intact",
          link.nextFrame(&f) && (f.len == 14u) &&
          (memcmp(f.payload, "ABCDEFGHIJKLMN", 14u) == 0));

    /* 7. outbound frames are well formed */
    uint8_t pl[1] = {0u};
    __stub_tx_n = 0;
    (void)link.send(CMD_HEARTBEAT, pl, 1u);
    check("send builds AA55 + crc",
          (__stub_tx_n == 7) && (__stub_tx[0] == 0xAA) && (__stub_tx[1] == 0x55));

    uint8_t v2[14] = {};
    v2[3] = 0x80; v2[6] = 255; v2[11] = 0x1c;
    v2[12] = 0xd8; v2[13] = 0x72;
    Telemetry decoded;
    check("v2 7S voltage and full fault word",
          Telemetry_Decode(v2, 14, &decoded) && decoded.bus_mv == 29400u &&
          decoded.volts_x10 == 294u && decoded.faults_all == 0x1c80u);
    v2[12] = 0; v2[13] = 0; v2[6] = 118; v2[11] = 0;
    check("legacy voltage still decodes",
          Telemetry_Decode(v2, 14, &decoded) && decoded.bus_mv == 11800u);

    printf("\nrx=%lu crc=%lu\n",
           (unsigned long)link.rxFrames(), (unsigned long)link.crcErrors());
    printf(fails ? "%d FAILED\n" : "all good\n", fails);

    return (fails != 0) ? 1 : 0;
}
