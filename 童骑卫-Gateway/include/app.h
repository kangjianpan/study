#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "telemetry.h"
#include "stm32_link.h"

/*  Shared state between the three application tasks.
 *
 *  Tasks, highest priority first:
 *    stm32Task  (10)  drains UART1, parses frames, sends the 1 Hz keepalive
 *    bleTask    ( 6)  10 Hz notify to the mini program, forwards commands
 *    netTask    ( 2)  cloud upload and the 4G modem, may block
 *
 *  The priority order is the whole point: uploading can be late, reading
 *  the STM32 cannot. */

struct DownCmd
{
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[16];
};

extern QueueHandle_t g_q_down;    /* bleTask -> stm32Task */
extern Stm32Link     g_link;

/*  Creates the mutex and queue. Call once from setup().  */
void App_InitShared(void);

void App_PublishTelemetry(const Telemetry *t, bool drive_allowed);
bool App_GetTelemetry(Telemetry *out, bool *drive_allowed);
void App_SetBleConnected(bool on);
bool App_GetBleConnected(void);
