#pragma once
// ============================================================
// config.example.h: template and build fallback for config.h.
//
// Copy this file to config.local.h (gitignored) and fill in the real
// WiFi credentials, device key, BLE pairing code and server host.
// include/config.h picks config.local.h when it exists, otherwise this
// file, so a fresh clone builds with the placeholders below. With the
// placeholders the device refuses all BLE commands and the server
// rejects its uploads.
//
// If you change a wire on the board, change the pin defs here.
// ============================================================

// ---- GPS: UART1 ----
// Rewired from GPIO3/1 (UART0, shared with USB) to avoid Serial Monitor conflicts
#define GPS_RX_PIN      4     // ESP32 RX ← GP-02 TX
#define GPS_TX_PIN      5     // ESP32 TX → GP-02 RX (optional for most use cases)
#define GPS_BAUD        9600

// ---- GSM SIM800L: UART2 ----
#define GSM_RX_PIN      16    // ESP32 RX ← SIM800L TxD
#define GSM_TX_PIN      17    // ESP32 TX → SIM800L RxD
#define GSM_RST_PIN     25    // Pull LOW 100ms → hard reset the module
#define GSM_PWRKEY_PIN  33    // Pull LOW 1s → power on / off
#define GSM_BAUD        9600

// ---- I2C: shared bus: OLED + MPU-6050 ----
#define SDA_PIN         21
#define SCL_PIN         22
#define MPU_I2C_ADDR    0x68  // AD0 pin tied LOW on GY-521 board
#define OLED_I2C_ADDR   0x3C  // SH1106 default address

// ---- Encoder EC11 (on OLED module board) ----
// GPIO34 and GPIO35 are INPUT-ONLY: NO internal pull-ups available!
// 10kΩ external pull-up resistors from TRA→3.3V and TRB→3.3V are REQUIRED.
#define ENC_A_PIN       35    // TRA: channel A
#define ENC_B_PIN       34    // TRB: channel B
#define ENC_BTN_PIN     32    // PSH: push button (INPUT_PULLUP available)

// ---- Battery ADC (Phase 5+, not in MVP) ----
#define BATTERY_ADC_PIN 36    // Voltage divider: 100kΩ + 47kΩ

// ---- Status LED ----
#define STATUS_LED_PIN  2     // Built-in blue LED on ESP32-WROOM-32

// ---- GSM APN (fill in before Phase 5) ----
// Egyptian SIM APNs:
//   Vodafone EG  → "internet.vodafone.net" (verified on hardware)
//   Orange EG    → "internet"
//   Etisalat/e&  → "etisalat"
//   WE           → "internet.te.eg"
#define GSM_APN         "internet.vodafone.net"  // Vodafone EG, verified 2026-10-16

// ===========================================================
// ---- WiFi (Phase 4.5) ----
// ===========================================================
#define WIFI_SSID        "your_wifi_ssid_here"
#define WIFI_PASSWORD    "your_wifi_password_here"
#define WIFI_TIMEOUT_MS  15000                  // ms to wait for association

// ===========================================================
// ---- Device authentication key ----
// Sent as X-Device-Key header on every HTTP request (WiFi + GSM).
// Must match this device's entry in the server's NAZER_DEVICE_KEYS
// ("DEVICE_ID:key"). One key per device, never reused.
// Generate one with: openssl rand -hex 24
// ===========================================================
#define DEVICE_SECRET_KEY  "your_device_secret_key_here"

// ===========================================================
// ---- BLE pairing code ----
// The app must send this before the device accepts any BLE command
// or shares telemetry over BLE (lib/Transport/BleAuth.h). 8 to 64
// characters, unique per device. Enter the same code in the app's
// Settings. The placeholder below disables all BLE commands.
// ===========================================================
#define BLE_PAIR_CODE      "your_ble_pairing_code_here"

// ===========================================================
// ---- Server endpoints ----
// Bench testing over WiFi: point SERVER_HOST at your laptop's LAN IP,
//   e.g. "http://192.168.1.10:8080", and run server.py or the platform API.
// ===========================================================
#define SERVER_HOST              "http://api.example.com"   // plain HTTP: SIM800L cannot do TLS
#define SERVER_PATH_TELEMETRY    "/v1/device/telemetry"
#define SERVER_PATH_VIOLATION    "/v1/device/violation"
#define SERVER_PATH_CONFIG       "/v1/device/config"
#define SERVER_PATH_EVENT        "/v1/device/event"

// ---- v2: tracking, modes, crash (2026-10-16) ----
#define NAZER_FW_VERSION          "3.0.0"
#define NAZER_HAS_DISPLAY         0        // prototype PCB: no OLED / encoder fitted
#define CLOUD_INTERVAL_MOVING_MS  60000    // normal moving batch interval
#define CLOUD_INTERVAL_PARKED_MS  1800000  // normal parked heartbeat interval
#define CRASH_GRACE_MS            30000    // time to cancel from the app before SMS goes out
#define BLE_LIVE_INTERVAL_MS      1000     // live telemetry to the phone

// ---- Speed / Violation Thresholds ----
#define MOVING_THRESHOLD_KMH      6.0f   // GPS speed must exceed this to be "moving"
#define MOVING_THRESHOLD_POOR_KMH 10.0f  // ...or this when HDOP is poor
#define STATIONARY_THRESHOLD_KMH  3.0f   // Speed must drop below this to be "stopped"
#define GPS_STATE_CONFIRM         5      // consecutive location updates (~3 s at 1 Hz RMC+GGA)
#define GPS_POOR_HDOP             2.5f   // above this the fix is too loose for small speeds
#define VIOLATION_MARGIN_KMH      5.0f   // Must exceed limit by this much to log violation
#define VIOLATION_MIN_DURATION_S  3      // Seconds over limit before issuing violation

// ---- Telemetry ----
#define TELEMETRY_INTERVAL_MS     10000  // Send data every 10 seconds

// ---- Kalman Filter Tuning (adjust after Phase 2+3 calibration drive) ----
#define KALMAN_Q                  0.5f   // Process noise: higher = trust IMU more
#define KALMAN_R_GPS              2.0f   // GPS measurement noise (set from stationary test)
#define KALMAN_R_IMU              0.1f   // IMU noise (from calibration data)

// ---- Logger level (see Logger.h for levels) ----
#define LOG_LEVEL                 0      // 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR, 4=NONE

// ---- Display refresh rate ----
#define DISPLAY_REFRESH_MS        100    // 10 Hz refresh