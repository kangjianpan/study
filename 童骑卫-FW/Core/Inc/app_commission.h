#ifndef APP_COMMISSION_H
#define APP_COMMISSION_H
/* Values stay unconfigured until the purchased hardware is measured.
 * UID allowlisting is identification, NOT cryptographic authentication. */
#ifndef APP_HELMET_UID_LEN
#define APP_HELMET_UID_LEN 0
#endif
#ifndef APP_HELMET_UID_BYTES
#define APP_HELMET_UID_BYTES {0}
#endif
#ifndef WHEEL_CIRCUMFERENCE_MM
#define WHEEL_CIRCUMFERENCE_MM 0U
#endif
#ifndef WHEEL_PULSES_PER_REV
#define WHEEL_PULSES_PER_REV 0U
#endif
#ifndef APP_WHEEL_VALIDATED
#define APP_WHEEL_VALIDATED 0
#endif
#ifndef APP_ESC_TIMING_VALIDATED
#define APP_ESC_TIMING_VALIDATED 0
#endif
#define ESC_NEUTRAL_DWELL_MS 3000U
#define WHEEL_MIN_EDGE_MS 2U
#define WHEEL_STOP_TIMEOUT_MS 2000U
#define WHEEL_MAX_KMH_X100 6000U
#define WHEEL_MISMATCH_MIN_X100 300U
#define TOF_INTERMEASUREMENT_MS 30U
#define TOF_STALE_MS 100U
#define IMU_STALE_MS 50U
#define SENSOR_INIT_TIMEOUT_MS 1500U
#define ARM_CLEAR_SETTLE_MS 30U

#if APP_DRIVE_COMMISSIONED && (!APP_WHEEL_VALIDATED || !APP_ESC_TIMING_VALIDATED || \
    !WHEEL_CIRCUMFERENCE_MM || !WHEEL_PULSES_PER_REV || \
    (APP_HELMET_UID_LEN != 4 && APP_HELMET_UID_LEN != 7 && APP_HELMET_UID_LEN != 10))
#error "Commission wheel geometry, wheel inputs, full helmet UID and ESC neutral timing before enabling drive."
#endif
#if APP_DRIVE_COMMISSIONED && !APP_ENABLE_IN_READY
#error "The hardware ARM latch requires MOTOR_EN in READY."
#endif
#endif
