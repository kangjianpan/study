#include "telemetry.h"

bool Telemetry_Decode(const uint8_t *p, uint8_t len, Telemetry *out)
{
    if ((p == nullptr) || (out == nullptr) || (len != 14u))
    {
        return false;
    }

    out->speed_x100 = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    out->limit_kmh  = p[2];
    out->faults_lo  = p[3];
    out->faults_all = (uint16_t)(p[3] | ((uint16_t)p[11] << 8));
    out->lap_ms10   = (uint16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8));
    out->bus_mv = (uint16_t)(p[12] | ((uint16_t)p[13] << 8));
    if (out->bus_mv == 0u) out->bus_mv = (uint16_t)p[6] * 100u;
    out->volts_x10 = out->bus_mv / 100u;
    out->wear       = p[7];
    out->trip_m     = (uint16_t)((uint16_t)p[8] | ((uint16_t)p[9] << 8));
    out->state      = p[10];
    out->valid      = true;
    return true;
}

const char *Telemetry_StateName(uint8_t state)
{
    switch (state)
    {
    case ST_SELF_TEST: return "SELF_TEST";
    case ST_STANDBY:   return "STANDBY";
    case ST_READY:     return "READY";
    case ST_RUN:       return "RUN";
    case ST_DECEL:     return "DECEL";
    case ST_FAULT:     return "FAULT_LOCK";
    default:           return "UNKNOWN";
    }
}
