#include "ble_link.h"
#include "app.h"
#include "config.h"

#include <NimBLEDevice.h>
#include <string>

BleLink g_ble;

static NimBLECharacteristic *s_telem_chr = nullptr;

/*----------------------------------------------------------------------------
 * Command whitelist
 *
 * Deliberately excludes 0x25 (forced throttle), 0x26 (forced speed),
 * 0x27 (inject wear failure) and 0x40 (fault injection). Those exist so the
 * bench can reproduce faults; exposing them over BLE would hand anyone with
 * a phone the ability to spoof the vehicle's own state.
 *--------------------------------------------------------------------------*/
static bool cmd_allowed(uint8_t cmd)
{
    switch (cmd)
    {
    case CMD_PARENT_LIMIT:
    case CMD_STOP:
    case CMD_RESUME:
    case CMD_VENUE_LIMIT:
    case CMD_CHILD_LIMIT:
        return true;
    default:
        return false;
    }
}

class ServerCallbacks : public NimBLEServerCallbacks
{
    void onConnect(NimBLEServer *server) override
    {
        (void)server;
        g_ble.setConnected(true);
        App_SetBleConnected(true);
        Serial.println("[ble] connected");
    }

    void onDisconnect(NimBLEServer *server) override
    {
        (void)server;
        g_ble.setConnected(false);
        App_SetBleConnected(false);
        Serial.println("[ble] disconnected, advertising again");
        NimBLEDevice::startAdvertising();
    }
};

class CommandCallbacks : public NimBLECharacteristicCallbacks
{
    void onWrite(NimBLECharacteristic *chr) override
    {
        NimBLEAttValue v = chr->getValue();
        g_ble.handleCommand(v.data(), v.size());
    }
};

void BleLink::begin(void)
{
    char info[64];

    NimBLEDevice::init(CFG_DEVICE_NAME);
    NimBLEDevice::setMTU(64);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    NimBLEService *svc = server->createService(CFG_BLE_SERVICE_UUID);

    s_telem_chr = svc->createCharacteristic(CFG_BLE_TELEM_UUID,
                                            NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *cmd_chr =
        svc->createCharacteristic(CFG_BLE_CMD_UUID,
                                  NIMBLE_PROPERTY::WRITE |
                                  NIMBLE_PROPERTY::WRITE_NR);
    cmd_chr->setCallbacks(new CommandCallbacks());

    snprintf(info, sizeof(info), "%s|fw=%s|mac=%s",
             CFG_DEVICE_NAME, CFG_FW_VERSION,
             NimBLEDevice::getAddress().toString().c_str());

    NimBLECharacteristic *info_chr =
        svc->createCharacteristic(CFG_BLE_INFO_UUID, NIMBLE_PROPERTY::READ);
    info_chr->setValue((uint8_t *)info, strlen(info));

    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(CFG_BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->start();
}

void BleLink::notifyTelemetry(const Telemetry *t)
{
    uint8_t buf[14];

    if ((!connected_) || (s_telem_chr == nullptr) || (t == nullptr) || (!t->valid))
    {
        return;
    }

    /* Repack from the struct so a change in the STM32 layout only touches
     * telemetry.cpp, not this file. */
    buf[0]  = (uint8_t)(t->speed_x100 & 0xFFu);
    buf[1]  = (uint8_t)(t->speed_x100 >> 8);
    buf[2]  = t->limit_kmh;
    buf[3]  = t->faults_lo;
    buf[4]  = (uint8_t)(t->lap_ms10 & 0xFFu);
    buf[5]  = (uint8_t)(t->lap_ms10 >> 8);
    buf[6]  = (uint8_t)((t->volts_x10 > 255u) ? 255u : t->volts_x10);
    buf[7]  = t->wear;
    buf[8]  = (uint8_t)(t->trip_m & 0xFFu);
    buf[9]  = (uint8_t)(t->trip_m >> 8);
    buf[10] = t->state;
    buf[11] = (uint8_t)(t->faults_all >> 8);
    buf[12] = (uint8_t)(t->bus_mv & 0xFFu);
    buf[13] = (uint8_t)(t->bus_mv >> 8);

    s_telem_chr->setValue(buf, sizeof(buf));
    s_telem_chr->notify();
}

void BleLink::handleCommand(const uint8_t *data, size_t len)
{
    DownCmd d;
    uint8_t plen;

    if ((data == nullptr) || (len < 2u))
    {
        return;
    }

    plen = data[1];
    if ((size_t)plen + 2u != len)
    {
        Serial.println("[ble] bad command length, dropped");
        return;
    }
    if ((plen > sizeof(d.payload)) || (!cmd_allowed(data[0])))
    {
        Serial.printf("[ble] rejected cmd 0x%02X\n", data[0]);
        return;
    }

    d.cmd = data[0];
    d.len = plen;
    for (uint8_t i = 0u; i < plen; i++)
    {
        d.payload[i] = data[2u + i];
    }

    /* Runs in the NimBLE host task: enqueue, never call g_link.send().  */
    if (xQueueSend(g_q_down, &d, 0) != pdTRUE)
    {
        Serial.println("[ble] downlink queue full, dropped");
    }
}
