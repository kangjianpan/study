#include "cloud.h"
#include "config.h"
#include "uplink.h"

#include <stdio.h>
#include <stdlib.h>

#include <ArduinoJson.h>

static Uplink  *s_up    = nullptr;
static uint32_t s_msg_id = 1u;
static uint32_t s_sent   = 0u;
static uint32_t s_failed = 0u;

/*  Last value that actually reached the cloud. -1 / 0 means "never sent",
 *  which forces the first packet to carry everything.  */
static int      s_last_speed  = -1;
static int      s_last_limit  = -1;
static int      s_last_state  = -1;
static int      s_last_faults = -1;
static int      s_last_volts  = -1;
static int      s_last_wear   = -1;
static int      s_last_trip   = -1;
static int      s_last_rssi   = 999;

static uint32_t s_t_speed  = 0u;
static uint32_t s_t_bat    = 0u;
static uint32_t s_t_trip   = 0u;
static uint32_t s_t_rssi   = 0u;
static uint32_t s_t_state  = 0u;
static uint32_t s_t_faults = 0u;

static bool wifi_configured(void)
{
    return String(CFG_WIFI_SSID) != "YOUR_SSID";
}

void Cloud_Begin(void)
{
    if (wifi_configured())
    {
        s_up = Uplink_WiFi();
        (void)s_up->begin();
    }
    else
    {
        Uplink *g = Uplink_4G();
        if (g != nullptr)
        {
            s_up = g;
            (void)s_up->begin();
        }
    }

    Serial.printf("[cloud] uplink=%s\n", Cloud_UplinkName());
}

const char *Cloud_UplinkName(void)
{
    return (s_up != nullptr) ? s_up->name() : "off";
}

uint32_t Cloud_Sent(void)   { return s_sent; }
uint32_t Cloud_Failed(void) { return s_failed; }

void Cloud_Tick(uint32_t now, const Telemetry &t)
{
    if ((s_up == nullptr) || (!t.valid))
    {
        return;
    }

    s_up->loop();
    if (!s_up->connected())
    {
        return;
    }

    /* ---- decide which properties are due (06.6.3) ---- */
    const bool send_speed =
        (now - s_t_speed >= CFG_CLOUD_SPEED_MS) &&
        ((s_last_speed < 0) ||
         (abs((int)t.speed_x100 - s_last_speed) >= CFG_CLOUD_SPEED_DELTA));

    const bool send_limit = ((int)t.limit_kmh != s_last_limit);

    const bool send_state =
        ((int)t.state != s_last_state) ||
        (now - s_t_state >= CFG_CLOUD_STATE_KEEP_MS);

    const bool send_faults =
        ((int)t.faults_all != s_last_faults) ||
        ((t.faults_all != 0u) && (now - s_t_faults >= CFG_CLOUD_FAULT_MS));

    const bool send_bat = (now - s_t_bat >= CFG_CLOUD_BATTERY_MS);

    const bool send_wear = ((int)t.wear != s_last_wear);

    const bool send_trip = (now - s_t_trip >= CFG_CLOUD_TRIP_MS);

    const int rssi = s_up->rssi();
    const bool send_rssi = (now - s_t_rssi >= CFG_CLOUD_RSSI_MS);

    if (!(send_speed || send_limit || send_state || send_faults ||
          send_bat || send_wear || send_trip || send_rssi))
    {
        return;
    }

    /* ---- build Alink JSON ---- */
    JsonDocument doc;
    doc["id"]      = String(s_msg_id);
    doc["version"] = "1.0";
    doc["method"]  = "thing.event.property.post";

    JsonObject params = doc["params"].to<JsonObject>();

    if (send_speed)  { params["speed"]   = (float)t.speed_x100 / 100.0f; }
    if (send_limit)  { params["limit"]   = t.limit_kmh; }
    if (send_state)  { params["state"]   = t.state; }
    if (send_faults) { params["faults"]  = t.faults_all; }
    if (send_bat)    { params["battery"] = (float)t.volts_x10 / 10.0f; }
    if (send_wear)   { params["wear"]    = t.wear; }
    if (send_trip)   { params["trip_m"]  = t.trip_m; }
    if (send_rssi && (rssi != 0)) { params["rssi"] = rssi; }

    char payload[CFG_CLOUD_PAYLOAD_MAX];
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    if ((n == 0u) || (n >= sizeof(payload)))
    {
        s_failed++;
        return;
    }

    char topic[160];
    snprintf(topic, sizeof(topic),
             "/sys/%s/%s/thing/event/property/post",
             CFG_ALIYUN_PRODUCT_KEY, CFG_ALIYUN_DEVICE_NAME);

    if (!s_up->publish(topic, payload))
    {
        s_failed++;
        return;                      /* keep the last_* values, retry later */
    }

    /* ---- commit ---- */
    s_sent++;
    s_msg_id++;

    if (send_speed)  { s_last_speed  = t.speed_x100; s_t_speed  = now; }
    if (send_limit)  { s_last_limit  = t.limit_kmh; }
    if (send_state)  { s_last_state  = t.state;      s_t_state  = now; }
    if (send_faults) { s_last_faults = t.faults_all;  s_t_faults = now; }
    if (send_bat)    { s_last_volts  = t.volts_x10;  s_t_bat    = now; }
    if (send_wear)   { s_last_wear   = t.wear; }
    if (send_trip)   { s_last_trip   = t.trip_m;     s_t_trip   = now; }
    if (send_rssi)   { s_last_rssi   = rssi;         s_t_rssi   = now; }
}
