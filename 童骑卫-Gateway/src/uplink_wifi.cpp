#include "uplink.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <mbedtls/md.h>

/*----------------------------------------------------------------------------
 * Aliyun MQTT credentials (一机一密)
 *
 *   clientId = "${deviceName}|securemode=2,signmethod=hmacsha256,timestamp=${ts}|"
 *   username = "${deviceName}&${productKey}"
 *   password = hmacsha256("clientId" + deviceName +
 *                         "deviceName" + deviceName +
 *                         "productKey" + productKey +
 *                         "timestamp"  + ts, deviceSecret)
 *
 * The console's "MQTT 连接参数生成" tool produces the same three strings.
 * Compare against it once before blaming this code.
 *
 * The timestamp is signed, not checked against the server clock, so a
 * build-time epoch plus uptime is enough for the handshake. Replace with
 * NTP once the uplink is stable (see README checklist).
 *--------------------------------------------------------------------------*/

static String hexString(const unsigned char *buf, size_t n)
{
    static const char *kHex = "0123456789abcdef";
    String s;

    s.reserve(n * 2u);
    for (size_t i = 0u; i < n; i++)
    {
        s += kHex[(buf[i] >> 4) & 0x0Fu];
        s += kHex[buf[i] & 0x0Fu];
    }
    return s;
}

static String hmacSha256Hex(const char *key, const String &msg)
{
    unsigned char out[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (info == nullptr)
    {
        return String("");
    }

    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, info, 1) != 0)
    {
        mbedtls_md_free(&ctx);
        return String("");
    }

    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)key, strlen(key));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)msg.c_str(), msg.length());
    mbedtls_md_hmac_finish(&ctx, out);
    mbedtls_md_free(&ctx);

    return hexString(out, sizeof(out));
}

static uint32_t aliyunTimestamp(void)
{
    return CFG_BUILD_EPOCH + (millis() / 1000u);
}

/*  securemode=2 是 TLS，3 是裸 TCP。本文件用 WiFiClientSecure（TLS），
 *  必须是 2；写成 3 时平台会按明文连接处理，握手行为不可预期。
 *  小程序端 utils/aliyun.js 同样固定为 2。 */
static String aliyunClientId(void)
{
    String s = CFG_ALIYUN_DEVICE_NAME;
    s += "|securemode=2,signmethod=hmacsha256,timestamp=";
    s += String((unsigned long)aliyunTimestamp());
    s += "|";
    return s;
}

static String aliyunUsername(void)
{
    String s = CFG_ALIYUN_DEVICE_NAME;
    s += "&";
    s += CFG_ALIYUN_PRODUCT_KEY;
    return s;
}

static String aliyunPassword(void)
{
    const String ts = String((unsigned long)aliyunTimestamp());
    String content;

    content.reserve(128);
    content += "clientId";
    content += CFG_ALIYUN_DEVICE_NAME;
    content += "deviceName";
    content += CFG_ALIYUN_DEVICE_NAME;
    content += "productKey";
    content += CFG_ALIYUN_PRODUCT_KEY;
    content += "timestamp";
    content += ts;

    return hmacSha256Hex(CFG_ALIYUN_DEVICE_SEC, content);
}

/*----------------------------------------------------------------------------
 * Uplink
 *--------------------------------------------------------------------------*/
class WifiUplink : public Uplink
{
public:
    bool begin(void) override;
    void loop(void) override;
    bool connected(void) override { return mqtt_.connected(); }
    bool publish(const char *topic, const char *payload) override;
    int  rssi(void) override { return (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0; }
    const char *name(void) const override { return "wifi"; }

private:
    bool ensureBroker(void);

    WiFiClientSecure secure_;
    PubSubClient     mqtt_{secure_};
    uint32_t         next_try_ms_ = 0u;
    uint8_t          backoff_s_  = 1u;
};

static WifiUplink s_wifi;

Uplink *Uplink_WiFi(void)
{
    return &s_wifi;
}

bool WifiUplink::begin(void)
{
    String host = CFG_ALIYUN_PRODUCT_KEY;
    host += ".iot-as-mqtt.";
    host += CFG_ALIYUN_REGION;
    host += ".aliyuncs.com";

    if (String(CFG_WIFI_SSID) == "YOUR_SSID")
    {
        Serial.println("[wifi] SSID not configured, skipped");
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);            /* BLE coexistence: keep the radio awake */
    WiFi.begin(CFG_WIFI_SSID, CFG_WIFI_PASS);

    const uint32_t t0 = millis();
    while ((WiFi.status() != WL_CONNECTED) &&
           ((millis() - t0) < CFG_WIFI_TIMEOUT_MS))
    {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[wifi] join failed");
        return false;
    }

    Serial.printf("[wifi] joined, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

#if CFG_ALIYUN_TLS
    secure_.setHandshakeTimeout(8);
#if CFG_ALIYUN_INSECURE
    secure_.setInsecure();          /* BENCH ONLY -- see config.h */
    Serial.println("[cloud] WARNING: TLS peer verification disabled (bench only)");
#else
    /* TODO: secure_.setCACert(ROOT_CA_PEM); */
    Serial.println("[cloud] FATAL: CFG_ALIYUN_INSECURE=0 but no CA installed");
    return false;
#endif
#endif

    mqtt_.setServer(host.c_str(), CFG_ALIYUN_PORT);
    mqtt_.setBufferSize(CFG_CLOUD_PAYLOAD_MAX + 64u);
    mqtt_.setKeepAlive(60);
    mqtt_.setSocketTimeout(2);

    return true;
}

bool WifiUplink::ensureBroker(void)
{
    if (mqtt_.connected())
    {
        return true;
    }
    if (millis() < next_try_ms_)
    {
        return false;
    }

    const String client_id = aliyunClientId();

    if (mqtt_.connect(client_id.c_str(),
                      aliyunUsername().c_str(),
                      aliyunPassword().c_str()))
    {
        Serial.println("[cloud] mqtt connected");
        backoff_s_ = 1u;
        return true;
    }

    Serial.printf("[cloud] mqtt connect failed, rc=%d\n", mqtt_.state());
    next_try_ms_ = millis() + (uint32_t)backoff_s_ * 1000u;
    backoff_s_   = (uint8_t)((backoff_s_ < 30u) ? (backoff_s_ * 2u) : 30u);
    return false;
}

void WifiUplink::loop(void)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }
    if (!mqtt_.connected())
    {
        (void)ensureBroker();
        return;
    }
    mqtt_.loop();
}

bool WifiUplink::publish(const char *topic, const char *payload)
{
    if (!mqtt_.connected())
    {
        return false;
    }
    return mqtt_.publish(topic, payload, false);
}
