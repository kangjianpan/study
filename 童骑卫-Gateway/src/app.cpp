#include "app.h"
#include <freertos/semphr.h>

QueueHandle_t g_q_down = nullptr;
Stm32Link     g_link;

static SemaphoreHandle_t s_snap_mutex = nullptr;
static Telemetry         s_snap;
static bool              s_snap_valid    = false;
static bool              s_drive_allowed = false;
static volatile bool     s_ble_connected = false;

void App_InitShared(void)
{
    s_snap_mutex = xSemaphoreCreateMutex();
    g_q_down     = xQueueCreate(8, sizeof(DownCmd));
}

void App_PublishTelemetry(const Telemetry *t, bool drive_allowed)
{
    if ((t == nullptr) || (!t->valid) || (s_snap_mutex == nullptr))
    {
        return;
    }

    if (xSemaphoreTake(s_snap_mutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        s_snap          = *t;
        s_snap_valid    = true;
        s_drive_allowed = drive_allowed;
        (void)xSemaphoreGive(s_snap_mutex);
    }
}

bool App_GetTelemetry(Telemetry *out, bool *drive_allowed)
{
    bool ok = false;

    if ((out == nullptr) || (s_snap_mutex == nullptr))
    {
        return false;
    }

    if (xSemaphoreTake(s_snap_mutex, pdMS_TO_TICKS(5)) == pdTRUE)
    {
        if (s_snap_valid)
        {
            *out = s_snap;
            if (drive_allowed != nullptr)
            {
                *drive_allowed = s_drive_allowed;
            }
            ok = true;
        }
        (void)xSemaphoreGive(s_snap_mutex);
    }

    return ok;
}

void App_SetBleConnected(bool on)
{
    s_ble_connected = on;
}

bool App_GetBleConnected(void)
{
    return s_ble_connected;
}
