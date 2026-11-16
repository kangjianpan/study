#include <Arduino.h>

#include "config.h"
#include "app.h"
#include "ble_link.h"
#include "cloud.h"

/*----------------------------------------------------------------------------
 * GuardKart gateway, ESP32-C3
 *
 * Three application tasks, and the priority order is the design:
 *
 *   stm32Task (10)  UART1 to the STM32. Reads, parses, answers. Runs first,
 *                   always. If this task ever waits on the network, the
 *                   STM32 sees a dead link and drops the car to 6 km/h --
 *                   the exact failure 03.2 of the design doc forbids.
 *   bleTask   ( 6)  10 Hz notify to the mini program.
 *   netTask   ( 2)  cloud upload and 4G housekeeping. Allowed to be late,
 *                   allowed to fail, allowed to block.
 *
 * There is no code path where the cloud can produce a frame for the STM32.
 *--------------------------------------------------------------------------*/

static void stm32Task(void *arg);
static void bleTask(void *arg);
static void netTask(void *arg);

void setup(void)
{
    Serial.begin(115200);
    delay(300);

    Serial.println();
    Serial.println("============================================");
    Serial.printf(" GuardKart gateway %s  dev=%s\n", CFG_FW_VERSION, CFG_DEVICE_NAME);
    Serial.println("============================================");

    App_InitShared();
    g_link.begin();
    g_ble.begin();
    Cloud_Begin();

    xTaskCreate(stm32Task, "stm32", 4096, nullptr, 10, nullptr);
    xTaskCreate(bleTask,   "ble",   3072, nullptr,  6, nullptr);
    xTaskCreate(netTask,   "net",   6144, nullptr,  2, nullptr);

    Serial.println("[app] tasks started");
}

void loop(void)
{
    /* Everything runs in tasks; loop() only prints the status line. */
    static uint32_t last = 0u;
    const uint32_t now = millis();

    if ((now - last) >= 5000u)
    {
        last = now;

        Telemetry t;
        bool allowed = false;

        if (App_GetTelemetry(&t, &allowed))
        {
            Serial.printf("[%lu] st=%-10s v=%u.%02u lim=%u f=0x%02X ble=%d up=%s "
                          "rx=%lu crc=%lu cloud=%lu/%lu\n",
                          (unsigned long)now,
                          Telemetry_StateName(t.state),
                          (unsigned)(t.speed_x100 / 100u),
                          (unsigned)(t.speed_x100 % 100u),
                          (unsigned)t.limit_kmh,
                          (unsigned)t.faults_lo,
                          App_GetBleConnected() ? 1 : 0,
                          Cloud_UplinkName(),
                          (unsigned long)g_link.rxFrames(),
                          (unsigned long)g_link.crcErrors(),
                          (unsigned long)Cloud_Sent(),
                          (unsigned long)Cloud_Failed());
        }
        else
        {
            Serial.printf("[%lu] no telemetry yet, rx=%lu crc=%lu\n",
                          (unsigned long)now,
                          (unsigned long)g_link.rxFrames(),
                          (unsigned long)g_link.crcErrors());
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}

/*----------------------------------------------------------------------------
 * STM32 link
 *--------------------------------------------------------------------------*/
static void stm32Task(void *arg)
{
    Telemetry tm = {};
    bool      drive_allowed = false;
    bool      dirty         = false;
    uint32_t  last_ka       = 0u;

    (void)arg;

    for (;;)
    {
        g_link.poll();

        RxFrame f;
        while (g_link.nextFrame(&f))
        {
            if ((f.cmd == CMD_TELEMETRY) &&
                Telemetry_Decode(f.payload, f.len, &tm))
            {
                dirty = true;
            }
            else if ((f.cmd == CMD_HEARTBEAT) && (f.len >= 1u))
            {
                drive_allowed = ((f.payload[0] & 0x01u) != 0u);
                dirty = true;
            }
        }

        if (dirty)
        {
            App_PublishTelemetry(&tm, drive_allowed);
            dirty = false;
        }

        /*  Keepalive. The STM32 goes LINK_STALE after 3 s of silence, so
         *  this must keep running even when BLE has no client at all.
         *  Payload 0: this direction carries no meaning beyond "I am here";
         *  the drive-allowed flag travels STM32 -> ESP32, not back.  */
        if ((millis() - last_ka) >= CFG_KEEPALIVE_MS)
        {
            const uint8_t zero = 0u;
            (void)g_link.send(CMD_HEARTBEAT, &zero, 1u);
            last_ka = millis();
        }

        DownCmd d;
        while (xQueueReceive(g_q_down, &d, 0) == pdTRUE)
        {
            if (!g_link.send(d.cmd, d.payload, d.len))
            {
                Serial.println("[link] send failed");
            }
        }

        vTaskDelay(1);
    }
}

/*----------------------------------------------------------------------------
 * BLE notify
 *--------------------------------------------------------------------------*/
static void bleTask(void *arg)
{
    (void)arg;

    for (;;)
    {
        Telemetry t;
        bool allowed = false;

        if (App_GetTelemetry(&t, &allowed) && g_ble.connected())
        {
            g_ble.notifyTelemetry(&t);
        }

        vTaskDelay(pdMS_TO_TICKS(CFG_BLE_NOTIFY_MS));
    }
}

/*----------------------------------------------------------------------------
 * Cloud
 *--------------------------------------------------------------------------*/
static void netTask(void *arg)
{
    (void)arg;

    for (;;)
    {
        Telemetry t;
        bool allowed = false;

        if (App_GetTelemetry(&t, &allowed))
        {
            Cloud_Tick(millis(), t);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
