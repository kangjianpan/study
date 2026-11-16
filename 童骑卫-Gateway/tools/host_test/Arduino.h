#pragma once

/*  Minimal Arduino shim so the frame parser can be built and run on a PC.
 *  Only what src/frame.cpp, src/telemetry.cpp and src/stm32_link.cpp use.
 *  If a source starts needing more of the Arduino API, add it here -- but
 *  keep this file dumb: it must never grow behaviour worth testing.  */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define SERIAL_8N1 0

unsigned long millis(void);
void          delay(unsigned long);

extern uint8_t __stub_rx[1024];
extern int     __stub_rx_n;
extern uint8_t __stub_tx[1024];
extern int     __stub_tx_n;

struct SerialStub
{
    void begin(unsigned long) {}
    void println(const char *) {}
    void println(void) {}
    int  printf(const char *, ...) { return 0; }
};

extern SerialStub Serial;

class HardwareSerial
{
public:
    explicit HardwareSerial(int n = 0) { (void)n; }

    void begin(unsigned long b, int c, int rx, int tx)
    {
        (void)b; (void)c; (void)rx; (void)tx;
    }

    int available(void) { return __stub_rx_n; }

    int read(void)
    {
        if (__stub_rx_n <= 0)
        {
            return -1;
        }
        const int c = __stub_rx[0];
        memmove(__stub_rx, __stub_rx + 1, (size_t)(--__stub_rx_n));
        return c;
    }

    size_t write(const uint8_t *p, size_t n)
    {
        if ((__stub_tx_n + (int)n) < 1024)
        {
            memcpy(__stub_tx + __stub_tx_n, p, n);
            __stub_tx_n += (int)n;
        }
        return n;
    }
};
