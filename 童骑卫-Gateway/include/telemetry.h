#pragma once

#include <stdint.h>

/*  The 14-byte payload of CMD 0x10, little endian (design doc 06.2).
 *  Field meanings are the STM32's; this side only reads them.  */
struct Telemetry
{
    uint16_t speed_x100;    /* [0..1]  km/h x 100                    */
    uint8_t  limit_kmh;     /* [2]     effective speed limit         */
    uint8_t  faults_lo;     /* [3]     FAULT_* bits 0..7             */
    uint16_t faults_all;    /* [3],[11] full v2 fault word           */
    uint16_t lap_ms10;      /* [4..5]  lap time, ms / 10 (0 = none)  */
    uint16_t volts_x10;     /* v2 [12..13] mV converted to 0.1V     */
    uint16_t bus_mv;        /* [12..13] mV; legacy fallback [6]     */
    uint8_t  wear;          /* [7]     bit0-1 seat, bit2-3 belt, 4-5 helmet */
    uint16_t trip_m;        /* [8..9]  trip distance, m              */
    uint8_t  state;         /* [10]    app_state_t                   */
    bool     valid;         /* decoded at least once                 */
};

bool        Telemetry_Decode(const uint8_t *p, uint8_t len, Telemetry *out);
const char *Telemetry_StateName(uint8_t state);

/*  App_state values, kept in sync with app_state.h.  */
enum
{
    ST_SELF_TEST = 0,
    ST_STANDBY   = 1,
    ST_READY     = 2,
    ST_RUN       = 3,
    ST_DECEL     = 4,
    ST_FAULT     = 5
};
