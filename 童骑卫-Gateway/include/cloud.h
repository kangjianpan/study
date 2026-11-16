#pragma once

#include <Arduino.h>
#include "telemetry.h"

/*  Aliyun IoT property upload (design doc 06.6).
 *
 *  Read-only, by construction: there is no subscribe() anywhere in the
 *  cloud path, so there is no code that could turn a cloud message into a
 *  command.  */

void Cloud_Begin(void);
void Cloud_Tick(uint32_t now_ms, const Telemetry &t);

const char *Cloud_UplinkName(void);   /* "wifi" / "4g" / "off" */
uint32_t    Cloud_Sent(void);
uint32_t    Cloud_Failed(void);
