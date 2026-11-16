#pragma once

#include <Arduino.h>

/*  One narrow interface for "get a JSON string to the cloud".
 *
 *  There are two implementations on purpose:
 *    WiFi  -- the normal case, and the one to bring up first
 *    4G    -- a second path for when the venue has no WiFi
 *
 *  Neither of them is allowed to grow a downlink. If you ever find yourself
 *  adding a subscribe() here, re-read 06.6.5 of the design doc.  */
class Uplink
{
public:
    virtual ~Uplink() {}

    virtual bool begin(void) = 0;
    virtual void loop(void) = 0;               /* reconnect / housekeeping */
    virtual bool connected(void) = 0;
    virtual bool publish(const char *topic, const char *payload) = 0;
    virtual int  rssi(void) = 0;               /* dBm, 0 if unknown       */
    virtual const char *name(void) const = 0;
};

Uplink *Uplink_WiFi(void);
Uplink *Uplink_4G(void);
