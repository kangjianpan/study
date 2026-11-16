#include "uplink.h"
#include "config.h"

/*----------------------------------------------------------------------------
 * 4G uplink -- SKELETON
 *
 * Status: handshake only. It brings up UART0, asks the modem whether it is
 * alive and whether the SIM is ready, then stops. MQTT over the modem is
 * NOT implemented, and publish() always returns false.
 *
 * Why it stops here on purpose: the AT dialect differs between modules
 * (Air780E / EC200S / EC800 and the Luat AT line all differ in the MQTT
 * command set and in how TLS credentials are provisioned). Inventing a
 * command sequence that looks plausible but was never run is worse than
 * leaving an obvious hole -- the team has to fill it from the manual of the
 * module actually bought.
 *
 * Fill in at the TODO below. Sequence that works for most Cat.1 modules:
 *
 *   1. AT                     module alive
 *   2. AT+CPIN?               SIM present, not PIN-locked
 *   3. AT+CSQ                 signal quality, want rssi >= 10
 *   4. AT+CGATT?              attached to packet domain
 *   5. AT+CGDCONT / PDP act   bearer up
 *   6. <vendor MQTT connect>  broker = <pk>.iot-as-mqtt.cn-shanghai.aliyuncs.com
 *   7. <vendor MQTT publish>  same topic and JSON as uplink_wifi.cpp
 *
 * Keep the rule from 3.2: the modem task runs at low priority. It must
 * never be able to delay the UART1 read loop.
 *--------------------------------------------------------------------------*/

#if CFG_4G_ENABLE

class At4gUplink : public Uplink
{
public:
    bool begin(void) override;
    void loop(void) override;
    bool connected(void) override { return online_; }
    bool publish(const char *topic, const char *payload) override;
    int  rssi(void) override { return rssi_dbm_; }
    const char *name(void) const override { return "4g"; }

private:
    bool at(const char *cmd, const char *expect, uint32_t timeout_ms);

    HardwareSerial modem_{CFG_4G_UART_NUM};
    bool     online_   = false;
    int      rssi_dbm_ = 0;
    uint32_t retry_at_ = 0u;
};

static At4gUplink s_4g;

Uplink *Uplink_4G(void)
{
    return &s_4g;
}

bool At4gUplink::at(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    modem_.print(cmd);
    modem_.print("\r\n");

    const uint32_t t0 = millis();
    String line;

    while ((millis() - t0) < timeout_ms)
    {
        if (modem_.available() > 0)
        {
            line = modem_.readStringUntil('\n');
            line.trim();
            if (line.indexOf(expect) >= 0)
            {
                return true;
            }
        }
        else
        {
            delay(10);
        }
    }
    return false;
}

bool At4gUplink::begin(void)
{
    modem_.begin(CFG_4G_BAUD, SERIAL_8N1, CFG_4G_RX_PIN, CFG_4G_TX_PIN);
    delay(100);

    if (!at("AT", "OK", 1000))
    {
        Serial.println("[4g] no AT response, modem absent?");
        return false;
    }
    Serial.println("[4g] modem alive");

    if (!at("AT+CPIN?", "READY", 2000))
    {
        Serial.println("[4g] SIM not ready");
        return false;
    }

    if (at("AT+CSQ", "+CSQ:", 1000))
    {
        /* "+CSQ: <rssi>,<ber>", rssi 0..31, 99 = unknown.
         * Rough mapping only: dBm = -113 + 2 * rssi. */
        /* TODO: parse the value instead of this placeholder. */
        rssi_dbm_ = 0;
    }

    /* TODO(step 5-6): PDP context + vendor MQTT connect. */
    Serial.println("[4g] AT handshake done; MQTT NOT implemented (see top of file)");
    online_ = false;
    return true;
}

void At4gUplink::loop(void)
{
    if (online_)
    {
        return;
    }
    if (millis() < retry_at_)
    {
        return;
    }
    retry_at_ = millis() + 30000u;
    /* TODO: reconnect sequence once publish() is implemented. */
}

bool At4gUplink::publish(const char *topic, const char *payload)
{
    (void)topic;
    (void)payload;
    return false;
}

#else /* !CFG_4G_ENABLE */

Uplink *Uplink_4G(void)
{
    /* Disabled at build time. cloud.cpp checks for nullptr and falls back
     * to WiFi only. */
    return nullptr;
}

#endif
