// =============================================================
// test_ble.cpp: Phase 4.5B: BLE Transport Test
//
// PURPOSE:
//   Verify that the ESP32 can:
//   1. Advertise as "NAZER-XXXX" (visible in nRF Connect scan)
//   2. Accept a phone connection on the custom GATT service
//   3. NOTIFY telemetry JSON every 10 seconds (enable notifications
//      on the Telemetry characteristic in nRF Connect)
//   4. Receive a speed limit command via the Command characteristic
//   5. Coexist with WiFi (both active simultaneously)
//
// TOOLS NEEDED:
//   nRF Connect for Mobile (free: Android/iOS)
//   https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-mobile
//
// TEST PROCEDURE:
//   1. Flash this sketch
//   2. Open Serial Monitor: note "NAZER-XXXX" device name
//   3. Open nRF Connect → SCAN → find "NAZER-XXXX"
//   4. Connect to device
//   5. Find service: 4FAFC201-...
//   6. Find Telemetry char: beb5483e-... → press NOTIFY button (bell icon)
//   7. Watch JSON arrive every 10 seconds
//   8. Find Command char: 1c95d5e3-... → press WRITE button
//      Write: {"cmd":"set_limit","value":80}
//   9. Check Serial: "Speed limit set to 80 km/h"
//
// PASS CRITERIA:
//   [ ] "NAZER-XXXX" visible in nRF Connect scan
//   [ ] Connection established (Serial: "BLE client connected")
//   [ ] Telemetry JSON arrives every 10 seconds
//   [ ] JSON contains real GPS lat/lon/speed (or zeros if no fix)
//   [ ] Writing set_limit command updates speed limit in Serial log
//   [ ] WiFi also connected simultaneously (if SSID/pass set in config.h)
//
// platformio.ini:
//   build_src_filter = -<*> +<../test/test_ble.cpp>
// =============================================================

#include <Arduino.h>
#include <Wire.h>

#include "Storage.h"
#include "DataBuilder.h"
#include "BLETransport.h"
#include "WiFiTransport.h"
#include "GPS.h"
#include "IMU.h"
#include "SensorFusion.h"
#include "config.h"

// ---- Global objects ----
SensorFusion fusion;
BLETransport  ble;
WiFiTransport wifi;   // also start WiFi to prove coexistence

static char latestTelJson[512];

// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n========================================");
    Serial.println("  NAZER Phase 4.5B: BLE Transport Test");
    Serial.println("========================================\n");

    // ---- Storage ----
    Storage.begin();
    Serial.printf("[SETUP] Device ID: %s\n", Storage.getDeviceId().c_str());

    // ---- I2C ----
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    // ---- Sensor fusion ----
    if (!fusion.begin()) {
        Serial.println("[SETUP] FATAL: IMU init failed");
        while (true) delay(1000);
    }

    // ---- BLE ----
    ble.connect(); // starts advertising
    Serial.printf("[SETUP] BLE advertising as: %s\n", ble.deviceName().c_str());
    Serial.printf("[SETUP] Use nRF Connect to connect and enable notifications\n");

    // ---- WiFi (coexistence test) ----
    // Comment this out if you want to test BLE-only mode
    Serial.printf("[SETUP] Starting WiFi alongside BLE (%s)...\n", WIFI_SSID);
    if (wifi.connect()) {
        Serial.printf("[SETUP] WiFi connected: %s\n", wifi.localIP().c_str());
    } else {
        Serial.println("[SETUP] WiFi not connected (OK for BLE-only test)");
    }

    latestTelJson[0] = '\0';
    Serial.println("[SETUP] Ready: connect phone via BLE\n");
}

// ============================================================
void loop() {
    // ---- Update sensors ----
    fusion.update();
    FusedData fused = fusion.getData();
    GPSData   gps   = fusion.gps().getData();
    IMUData   imu   = fusion.imu().getData();

    // ---- Build latest telemetry JSON ----
    static uint32_t lastBuildMs = 0;
    if (millis() - lastBuildMs >= 1000) { // rebuild every second
        lastBuildMs = millis();
        DataBuilder::buildTelemetry(
            Storage.getDeviceId(),
            fused, gps, imu,
            Storage.getSpeedLimit(),
            0, 0,
            latestTelJson, sizeof(latestTelJson)
        );
    }

    // ---- BLE tick: handles NOTIFY timer ----
    ble.tick(latestTelJson);

    // ---- Log connection status changes ----
    static bool wasConnected = false;
    bool isConn = ble.isConnected();
    if (isConn != wasConnected) {
        wasConnected = isConn;
        Serial.printf("[LOOP] BLE client %s\n", isConn ? "CONNECTED" : "DISCONNECTED");
    }

    // ---- Check if phone sent a speed limit command ----
    static int lastCmdLimit = 0;
    int cmdLimit = ble.lastReceivedSpeedLimit();
    if (cmdLimit > 0 && cmdLimit != lastCmdLimit) {
        lastCmdLimit = cmdLimit;
        // Storage.setSpeedLimit() already called inside NazerCommandCallbacks::onWrite()
        Serial.printf("[LOOP] Speed limit from BLE command: %d km/h\n", cmdLimit);
    }

    // ---- Periodic status log ----
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs >= 5000) {
        lastStatusMs = millis();
        Serial.printf("[LOOP] BLE=%s  WiFi=%s  fused=%.1f km/h  sats=%d  limit=%d km/h\n",
                      ble.isConnected()  ? "connected" : "advertising",
                      wifi.isConnected() ? "up"        : "down",
                      fused.fusedSpeedKmh,
                      gps.satellites,
                      Storage.getSpeedLimit());
    }
}