#pragma once

/*  All tunables live here. Nothing else in the project should contain a
 *  magic number. Mirrors app_config.h on the STM32 side: if a value is
 *  duplicated across the two boards, it is written here as well and this
 *  file is the reference.  */

/*----------------------------------------------------------------------------
 * Board constraints
 *
 * ESP32-C3 has exactly two UART controllers (UART0, UART1). Both are needed:
 * one for the STM32 link, one for the 4G modem. Debug output therefore has
 * to go over USB Serial/JTAG, which requires ARDUINO_USB_CDC_ON_BOOT=1.
 * If this trips, you either forgot the build flag or you swapped the board
 * for one without USB-CDC -- in that case use an ESP32-S3 (three UARTs).
 *--------------------------------------------------------------------------*/
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT == 0)
#error "USB CDC On Boot must be Enabled: UART0 is reserved for the 4G modem."
#endif

/*----------------------------------------------------------------------------
 * Device identity
 *--------------------------------------------------------------------------*/
#define CFG_DEVICE_NAME        "GuardKart-01"   /* last two chars = car number */
#define CFG_FW_VERSION         "0.1.0"

/*----------------------------------------------------------------------------
 * STM32 link -- UART1
 * The STM32 side is USART3 on PB10/PB11, 115200 8N1 (see 方案 4.2).
 *--------------------------------------------------------------------------*/
#define CFG_STM32_UART_NUM     1
#define CFG_STM32_RX_PIN       4
#define CFG_STM32_TX_PIN       5
#define CFG_STM32_BAUD         115200

#define CFG_FRAME_HDR1         0xAA
#define CFG_FRAME_HDR2         0x55
#define CFG_FRAME_MAX_PAYLOAD  32
#define CFG_DRAIN_MAX_BYTES    256   /* per poll; keeps the reader non-blocking */

/* The STM32 drops to LIMIT_FALLBACK=6 km/h if it hears nothing for 3 s.
 * Keep this well under 3000 ms. */
#define CFG_KEEPALIVE_MS       1000

/*----------------------------------------------------------------------------
 * BLE (06.3 of the design doc)
 *--------------------------------------------------------------------------*/
#define CFG_BLE_SERVICE_UUID   "0000fff0-0000-1000-8000-00805f9b34fb"
#define CFG_BLE_TELEM_UUID     "0000fff1-0000-1000-8000-00805f9b34fb"
#define CFG_BLE_CMD_UUID       "0000fff2-0000-1000-8000-00805f9b34fb"
#define CFG_BLE_INFO_UUID      "0000fff3-0000-1000-8000-00805f9b34fb"
#define CFG_BLE_NOTIFY_MS      100        /* 10 Hz, matches TelemetryTask */

/*----------------------------------------------------------------------------
 * Aliyun IoT (06.6 of the design doc)
 *
 * Fill these in from the console: 设备详情 -> 三元组.
 * CFG_ALIYUN_TLS 1 uses port 8883. Set CFG_ALIYUN_INSECURE 1 ONLY on the
 * bench while you are still fighting certificates -- it disables peer
 * verification and must be 0 in anything shown to a judge.
 *--------------------------------------------------------------------------*/
#define CFG_ALIYUN_PRODUCT_KEY  "YOUR_PK"
#define CFG_ALIYUN_DEVICE_NAME  "GuardKart-01"
#define CFG_ALIYUN_DEVICE_SEC   "YOUR_DEVICE_SECRET"
#define CFG_ALIYUN_REGION       "cn-shanghai"

#define CFG_ALIYUN_TLS          1
/*  1 = skip certificate verification. Defaults to 1 so the project builds
 *  and runs on day one; it is a BENCH setting. Before anything user-facing,
 *  set it to 0 and add the root CA in src/uplink_wifi.cpp. The boot banner
 *  prints a warning while this is 1. */
#define CFG_ALIYUN_INSECURE     1
#define CFG_ALIYUN_PORT         (CFG_ALIYUN_TLS ? 8883 : 1883)

/* A fixed build-time epoch is enough for the signature: the server hashes
 * the timestamp we send, it does not compare it against its own clock.
 * Replace with NTP once the uplink is up. */
#define CFG_BUILD_EPOCH         1760000000UL

/*----------------------------------------------------------------------------
 * WiFi / 4G uplink selection
 *
 * CFG_UPLINK_AUTO 1: WiFi when it is configured and reachable, else 4G.
 * CFG_4G_ENABLE   1: start the modem task. Leave 0 until the AT sequence
 *                    for YOUR module is filled in (src/uplink_at4g.cpp).
 *--------------------------------------------------------------------------*/
#define CFG_UPLINK_AUTO         1
#define CFG_WIFI_SSID          "YOUR_SSID"
#define CFG_WIFI_PASS          "YOUR_PASS"
#define CFG_WIFI_TIMEOUT_MS    15000

#define CFG_4G_ENABLE           0
#define CFG_4G_UART_NUM         0        /* UART0 = GPIO20/21 on C3 */
#define CFG_4G_RX_PIN           20
#define CFG_4G_TX_PIN           21
#define CFG_4G_BAUD             115200

/*----------------------------------------------------------------------------
 * Cloud downsampling (06.6.3)
 *--------------------------------------------------------------------------*/
#define CFG_CLOUD_SPEED_MS      1000
#define CFG_CLOUD_SPEED_DELTA   10       /* 0.10 km/h, in x100 units */
#define CFG_CLOUD_BATTERY_MS    5000
#define CFG_CLOUD_TRIP_MS       20000
#define CFG_CLOUD_RSSI_MS       5000
#define CFG_CLOUD_STATE_KEEP_MS 60000
#define CFG_CLOUD_FAULT_MS      1000     /* only while faults != 0 */
#define CFG_CLOUD_PAYLOAD_MAX   384

/*----------------------------------------------------------------------------
 * Protocol command bytes (06.1). Kept identical to app_comm.c.
 *--------------------------------------------------------------------------*/
#define CMD_TELEMETRY           0x10
#define CMD_PARENT_LIMIT        0x20
#define CMD_STOP                0x21
#define CMD_RESUME              0x22
#define CMD_VENUE_LIMIT         0x23
#define CMD_CHILD_LIMIT         0x24
#define CMD_HEARTBEAT           0x30
