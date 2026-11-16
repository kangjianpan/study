#pragma once

#include <Arduino.h>
#include "telemetry.h"

/*  BLE GATT server, service FFF0 (design doc 06.3).
 *
 *  FFF1  Notify  14-byte telemetry, 10 Hz
 *  FFF2  Write   [CMD][LEN][PAYLOAD..]  -- the mini program never builds a
 *                full AA55 frame and never computes a CRC. This side wraps
 *                it. Keeps the phone code trivial and keeps framing in one
 *                place.
 *  FFF3  Read    device name / firmware / MAC
 *
 *  Only the four whitelisted commands (0x20..0x24) are forwarded. The debug
 *  commands 0x25..0x27 / 0x40 exist for the bench and must not be reachable
 *  from a phone that any child could be holding. */

class BleLink
{
public:
    void begin(void);
    void notifyTelemetry(const Telemetry *t);
    bool connected(void) const { return connected_; }

    /*  Called from the NimBLE host task; must not block.  */
    void handleCommand(const uint8_t *data, size_t len);

    void setConnected(bool on) { connected_ = on; }

private:
    volatile bool connected_ = false;
};

extern BleLink g_ble;
