// =============================================================
// test_storage.cpp: Phase 4.5C: LocalStorage Offline Buffer Test
//
// PURPOSE:
//   Verify the NVS offline buffer works correctly:
//   1. Queue 10 telemetry records while server is unreachable
//   2. Queue 1 violation at 15s (regardless of WiFi state)
//   3. Power cycle: records must survive reboot
//   4. On reconnect, violation replays BEFORE telemetry
//   5. Ring buffer wraps at 50 (oldest overwritten)
//
// TEST PROCEDURE:
//   Phase A: Offline queue (server DOWN):
//     1. Make sure server.py is NOT running on your laptop
//     2. Flash and open Serial Monitor
//     3. Wait ~100 seconds: watch 10 telemetry records queue
//     4. At 15s: 1 violation also queues automatically
//     5. When Serial prints "10 records queued": power off ESP32
//
//   Phase B: Survive reboot:
//     6. Power on again: Serial must show:
//        [LOCALSTORAGE] Ready: telemetry: 10/50  violations: 1/20
//
//   Phase C: Replay (server UP):
//     7. Start server:
//        cd device   (this folder)
//        uvicorn server:app --host 0.0.0.0 --port 8080
//     8. Reboot ESP32: violation replays first, then 10 telemetry
//     9. Server shows 1 violation + 10 telemetry in order
//     10. Serial shows: "Replay complete: 11 records sent"
//     11. Storage status: telemetry: 0/50  violations: 0/20
//
//   Phase D: Ring wrap (optional):
//     12. Stop server, uncomment #define FORCE_RING_WRAP
//     13. Reflash: writes 60 records, ring wraps at 50
//     14. Start server: only 50 records arrive (oldest 10 dropped)
//
// PASS CRITERIA:
//   [ ] 10 telemetry + 1 violation queued while offline
//   [ ] Records survive power cycle (NVS persistence)
//   [ ] Violation replays before telemetry
//   [ ] All 11 records arrive at server in correct order
//   [ ] Storage shows 0/50 and 0/20 after replay
//   [ ] Ring wraps correctly: 60 written → 50 survive
//
// platformio.ini:
//   build_src_filter = -<*> +<../test/test_storage.cpp>
// =============================================================

#include <Arduino.h>
#include <Wire.h>

#include "Storage.h"
#include "DataBuilder.h"
#include "LocalStorage.h"
#include "WiFiTransport.h"
#include "GPS.h"
#include "IMU.h"
#include "SensorFusion.h"
#include "config.h"

// ---- Uncomment to test ring wrap (writes 60 records then halts) ----
// #define FORCE_RING_WRAP

// ---- Uncomment to wipe NVS on boot (use if NVS got into bad state) ----
// #define CLEAR_ON_BOOT

SensorFusion  fusion;
WiFiTransport wifi;
LocalStorage  ls;

static char telBuf[512];
static char violBuf[256];

// ============================================================
void printStorageStatus() {
    ls.printStatus();
    Serial.printf("[STATUS] WiFi connected: %s\n",
                  wifi.isConnected() ? "YES" : "NO");
}

// ============================================================
// Print reminder after 10 records queued.
// Defined BEFORE loop() so the compiler sees it.
// ============================================================
void _checkQueueReminder(uint8_t count) {
    if (count == 10) {
        Serial.println("\n[LOOP] *** 10 records queued ***");
        Serial.println("  Next steps:");
        Serial.println("  1. Power off ESP32 (unplug USB)");
        Serial.println("  2. Power on: Serial must show telemetry: 10/50  violations: 1/20");
        Serial.println("  3. Start server.py on laptop");
        Serial.println("  4. Watch violation + 10 telemetry replay in order\n");
    }
}

// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n============================================");
    Serial.println("  NAZER Phase 4.5C: LocalStorage Test");
    Serial.println("============================================\n");

    Storage.begin();
    Serial.printf("[SETUP] Device ID: %s\n", Storage.getDeviceId().c_str());

    ls.begin();

#ifdef CLEAR_ON_BOOT
    Serial.println("[SETUP] CLEAR_ON_BOOT: erasing all queued records");
    ls.clearAll();
#endif

    Serial.println("[SETUP] Storage status on boot:");
    printStorageStatus();

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
    if (!fusion.begin()) {
        Serial.println("[SETUP] FATAL: IMU init failed");
        while (true) delay(1000);
    }

    Serial.printf("[SETUP] Connecting WiFi: %s\n", WIFI_SSID);
    if (wifi.connect()) {
        Serial.printf("[SETUP] WiFi connected: %s\n", wifi.localIP().c_str());
        if (ls.telemetryCount() > 0 || ls.violationCount() > 0) {
            Serial.printf("[SETUP] Found %d tel + %d viol from previous session: replaying...\n",
                          ls.telemetryCount(), ls.violationCount());
            uint8_t n = ls.replayAll(&wifi);
            Serial.printf("[SETUP] Replay sent %d records\n", n);
        }
    } else {
        Serial.println("[SETUP] WiFi not connected: will queue records offline");
    }

#ifdef FORCE_RING_WRAP
    Serial.println("\n[SETUP] FORCE_RING_WRAP: writing 60 records...");
    for (int i = 0; i < 60; i++) {
        fusion.update();
        FusedData fd = fusion.getData();
        GPSData   gd = fusion.gps().getData();
        IMUData   id = fusion.imu().getData();

        size_t len = DataBuilder::buildTelemetry(
            Storage.getDeviceId(), fd, gd, id,
            Storage.getSpeedLimit(), 0, 0,
            telBuf, sizeof(telBuf));
        if (len > 0) ls.enqueueTelemetry(telBuf);

        Serial.printf("[SETUP] Wrote %d/60  queued=%d\n", i + 1, ls.telemetryCount());
        delay(50);
    }
    Serial.println("\n[SETUP] Ring wrap done. Power cycle, then start server to test replay.");
    printStorageStatus();
    while (true) delay(1000);
#endif

    Serial.println("\n[SETUP] Ready: starting main loop\n");
}

// ============================================================
void loop() {
    fusion.update();
    FusedData fused = fusion.getData();
    GPSData   gps   = fusion.gps().getData();
    IMUData   imu   = fusion.imu().getData();

    static uint32_t lastTelMs  = 0;
    static uint32_t lastReplay = 0;
    static uint8_t  telCount   = 0;

    // ---- Telemetry every 10 seconds ----
    if (millis() - lastTelMs >= TELEMETRY_INTERVAL_MS) {
        lastTelMs = millis();

        size_t len = DataBuilder::buildTelemetry(
            Storage.getDeviceId(), fused, gps, imu,
            Storage.getSpeedLimit(), 0, 0,
            telBuf, sizeof(telBuf));

        if (len == 0) {
            Serial.println("[LOOP] DataBuilder failed");
            return;
        }

        if (wifi.isConnected()) {
            bool ok = wifi.sendTelemetry(telBuf);
            if (ok) {
                Serial.printf("[LOOP] Telemetry SENT (online)  fused=%.1f km/h\n",
                              fused.fusedSpeedKmh);
                int limit = wifi.lastReceivedSpeedLimit();
                if (limit > 0) Storage.setSpeedLimit(limit);
            } else {
                // Server unreachable even though WiFi is up: queue offline
                ls.enqueueTelemetry(telBuf);
                telCount++;
                Serial.printf("[LOOP] WiFi send failed: queued offline  [%d/50]\n", telCount);
                _checkQueueReminder(telCount);
            }
        } else {
            ls.enqueueTelemetry(telBuf);
            telCount++;
            Serial.printf("[LOOP] Queued offline #%d  total=%d/%d\n",
                          telCount, ls.telemetryCount(), LocalStorage::MAX_TELEMETRY);
            _checkQueueReminder(telCount);
        }
    }

    // ---- Queue one test violation at 15 seconds ----
    // Queues regardless of WiFi state to test the violation
    // FIFO independently of the online path.
    static bool violQueued = false;
    if (!violQueued && millis() >= 15000) {
        violQueued = true;

        FusedData fakeFused      = fused;
        fakeFused.fusedSpeedKmh  = 95.0f;

        size_t len = DataBuilder::buildViolation(
            Storage.getDeviceId(), fakeFused, 80.0f, 12,
            violBuf, sizeof(violBuf));

        if (len > 0) {
            ls.enqueueViolation(violBuf);
            Serial.println("[LOOP] Test violation queued: will replay before telemetry");
        }
    }

    // ---- Replay queued records when server is reachable ----
    if (wifi.isConnected()) {
        uint32_t now = millis();
        if ((ls.telemetryCount() > 0 || ls.violationCount() > 0) &&
            now - lastReplay >= 5000) {
            lastReplay = now;
            Serial.printf("[LOOP] Starting replay (%d tel, %d viol queued)...\n",
                          ls.telemetryCount(), ls.violationCount());
            uint8_t sent = ls.replayAll(&wifi);
            if (sent > 0) {
                Serial.printf("[LOOP] Replay: sent %d  remaining: tel=%d viol=%d\n",
                              sent, ls.telemetryCount(), ls.violationCount());
            }
        }
    }

    // ---- Status every 30 seconds ----
    static uint32_t lastStatusMs = 0;
    if (millis() - lastStatusMs >= 30000) {
        lastStatusMs = millis();
        printStorageStatus();
    }
}