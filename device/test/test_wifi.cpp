// =============================================================
// test_wifi.cpp: Phase 4.5A: WiFi Transport Test
//
// PURPOSE:
//   Verify that the ESP32 can:
//   1. Connect to home WiFi
//   2. Build correct JSON from live GPS + IMU data
//   3. POST /v1/device/telemetry → parse speed_limit_kmh response
//   4. POST /v1/device/violation → parse violation_id response
//   5. Auto-reconnect if WiFi drops
//
// BEFORE FLASHING:
//   Edit include/config.h:
//     #define WIFI_SSID       "YourNetworkName"
//     #define WIFI_PASSWORD   "YourPassword"
//     #define WIFI_TIMEOUT_MS  15000
//     #define SERVER_HOST     "http://192.168.x.x:8080"   ← laptop IP
//
// ON YOUR LAPTOP:
//   Run: python server.py    (from NAZER_PHASE_4_5.md)
//   Install: pip install fastapi uvicorn
//
// PASS CRITERIA (check each in Serial Monitor):
//   [ ] "NAZER_XXXXXXXX" device ID printed at boot
//   [ ] "Connected: IP=..." within 15 seconds
//   [ ] POST /v1/device/telemetry returns 200
//   [ ] Server logs show real GPS lat/lon/speed
//   [ ] Response "speed_limit_kmh" parsed and printed
//   [ ] POST /v1/device/violation returns 200
//   [ ] "violation_id" printed in Serial
//   [ ] After turning off WiFi router for 30s, reconnects automatically
//
// platformio.ini:
//   build_src_filter = -<*> +<../test/test_wifi.cpp>
// =============================================================

#include <Arduino.h>
#include <Wire.h>

#include "Storage.h"
#include "DataBuilder.h"
#include "WiFiTransport.h"
#include "GPS.h"
#include "IMU.h"
#include "SensorFusion.h"
#include "config.h"

// ---- Global objects ----
SensorFusion fusion;
WiFiTransport wifi;

// ---- Timing ----
static uint32_t lastTelemetryMs  = 0;
static uint32_t lastViolationMs  = 0;
static bool     violationSent    = false;  // send one test violation then stop

// ---- Buffer sizes ----
static constexpr size_t TEL_BUF_SIZE  = 512;
static constexpr size_t VIOL_BUF_SIZE = 256;

// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("  NAZER Phase 4.5A: WiFi Transport Test");
    Serial.println("========================================\n");

    // ---- NVS / Storage ----
    Storage.begin();
    String deviceId = Storage.getDeviceId();
    Serial.printf("[SETUP] Device ID: %s\n", deviceId.c_str());

    // ---- I2C for IMU ----
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000); // 400 kHz: faster I2C for IMU reads

    // ---- Sensor fusion (GPS + IMU + Kalman) ----
    if (!fusion.begin()) {
        Serial.println("[SETUP] FATAL: IMU init failed: check wiring");
        while (true) { delay(1000); }
    }
    Serial.println("[SETUP] SensorFusion OK");

    // ---- WiFi ----
    Serial.printf("[SETUP] Connecting to WiFi: %s\n", WIFI_SSID);
    if (!wifi.connect()) {
        // Non-fatal in test: we'll retry in loop()
        Serial.println("[SETUP] Initial WiFi connect failed: will retry in loop()");
    }

    // ---- CSV header for Serial log ----
    Serial.println("\n---- CSV Log ----");
    SensorFusion::printCsvHeader();
}

// ============================================================
void loop() {
    // ---- Update sensors every iteration ----
    fusion.update();
    FusedData fused = fusion.getData();
    GPSData   gps   = fusion.gps().getData();
    IMUData   imu   = fusion.imu().getData();

    // ---- CSV row every second ----
    static uint32_t lastCsvMs = 0;
    if (millis() - lastCsvMs >= 1000) {
        lastCsvMs = millis();
        fusion.printCsvRow();
    }

    // ---- Telemetry POST every TELEMETRY_INTERVAL_MS ----
    if (millis() - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMs = millis();

        char buf[TEL_BUF_SIZE];
        size_t len = DataBuilder::buildTelemetry(
            Storage.getDeviceId(),
            fused,
            gps,
            imu,
            Storage.getSpeedLimit(), // current speed limit (default 60)
            0,    // battPct: ADC not wired in Phase 4.5
            0,    // gsmSignal: GSM not active
            buf,
            sizeof(buf)
        );

        if (len == 0) {
            Serial.println("[LOOP] DataBuilder: telemetry JSON failed");
        } else {
            Serial.printf("\n[LOOP] Sending telemetry (%u bytes)...\n", (unsigned)len);
            Serial.printf("[LOOP] Payload: %s\n", buf);

            bool ok = wifi.sendTelemetry(buf);

            if (ok) {
                int newLimit = wifi.lastReceivedSpeedLimit();
                if (newLimit > 0 && newLimit != Storage.getSpeedLimit()) {
                    Storage.setSpeedLimit(newLimit);
                    Serial.printf("[LOOP] Speed limit updated from server: %d km/h\n", newLimit);
                }
                Serial.printf("[LOOP] Telemetry OK: fused=%.1f km/h  gps=%.1f km/h  limit=%d km/h\n",
                              fused.fusedSpeedKmh,
                              gps.rawSpeedKmh,
                              Storage.getSpeedLimit());
            } else {
                Serial.printf("[LOOP] Telemetry FAILED (failures=%d)\n",
                              wifi.consecutiveFailures());
            }
        }
    }

    // ---- Test violation POST: send once after 30 seconds ----
    // Simulates a violation regardless of actual speed, just to verify
    // the endpoint and response parsing work. A real violation fires
    // based on speed in the full firmware.
    if (!violationSent && millis() >= 30000) {
        violationSent = true;

        // Pretend we were doing 95 km/h in a 80 km/h zone for 12 seconds
        FusedData fakeFused = fused;
        fakeFused.fusedSpeedKmh = 95.0f;

        char buf[VIOL_BUF_SIZE];
        size_t len = DataBuilder::buildViolation(
            Storage.getDeviceId(),
            fakeFused,
            80.0f,  // speed limit
            12,     // duration in seconds
            buf,
            sizeof(buf)
        );

        if (len > 0) {
            Serial.printf("\n[LOOP] Sending test violation (%u bytes)...\n", (unsigned)len);
            Serial.printf("[LOOP] Payload: %s\n", buf);

            bool ok = wifi.sendViolation(buf);
            if (ok) {
                Serial.printf("[LOOP] Violation OK: ID: %s\n",
                              wifi.lastViolationId().c_str());
            } else {
                Serial.printf("[LOOP] Violation FAILED (failures=%d)\n",
                              wifi.consecutiveFailures());
            }
        }
    }

    // ---- WiFi health check: log RSSI every 10 seconds ----
    static uint32_t lastRssiMs = 0;
    if (wifi.isConnected() && millis() - lastRssiMs >= 10000) {
        lastRssiMs = millis();
        Serial.printf("[LOOP] WiFi RSSI: %d dBm\n", wifi.rssi());
    }
}