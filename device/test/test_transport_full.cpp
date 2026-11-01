// =============================================================
// test_transport_full.cpp: Phase 4.5D: Full Transport Stack
//
// Tests TransportManager end-to-end:
//   1. WiFi connect → telemetry POST → server speed limit received
//   2. BLE advertising started simultaneously (coexistence)
//   3. WiFi disconnect simulation → failover to OFFLINE
//   4. Records queue in LocalStorage while offline
//   5. WiFi reconnect → queued records replay to server
//   6. Violation queuing and replay (violations before telemetry)
//   7. BLE → WiFi priority: WiFi wins when both available
//
// To flash:
//   platformio.ini → build_src_filter = -<*> +<../test/test_transport_full.cpp>
//
// Server must be running on laptop:
//   uvicorn server:app --host 0.0.0.0 --port 8080
//
// Pass criteria (all must be met before marking 4.5D done):
//   [ ] BOOT: State = WIFI_ACTIVE on startup
//   [ ] STEP 1: 3x telemetry POSTs succeed (HTTP 200), speed limit received
//   [ ] STEP 1: BLE advertising visible in nRF Connect simultaneously
//   [ ] STEP 2: WiFi killed → State transitions to OFFLINE
//   [ ] STEP 3: 3x telemetry + 1x violation queued to NVS while offline
//   [ ] STEP 4: WiFi restored → State transitions back to WIFI_ACTIVE
//   [ ] STEP 4: Violation replayed BEFORE telemetry records
//   [ ] STEP 4: All 4 queued records delivered to server (HTTP 200)
//   [ ] STEP 5: Speed limit update via BLE write → confirmed in Serial
//   [ ] STEP 5: mgr.lastReceivedSpeedLimit() returns BLE value when WiFi has none
// =============================================================

#include <Arduino.h>
#include "TransportManager.h"
#include "Storage.h"
#include "DataBuilder.h"
#include "SensorFusion.h"
#include "GPS.h"
#include "IMU.h"

// ---- Test configuration ----
#define STEP_DELAY_MS         3000    // Pause between test steps (ms)
#define TELEMETRY_SENDS       3       // Number of telemetry sends in each online step
#define OFFLINE_QUEUE_TEL     3       // Telemetry records to queue while offline
#define OFFLINE_QUEUE_VIOL    1       // Violations to queue while offline

// ---- Fake sensor data (stationary, no GPS fix) ----
// Matches the indoor test conditions from Phase 4.5A/B/C.
// Real GPS data will come in Phase 7 (integration).
static FusedData makeFusedData(float speedKmh = 0.0f) {
    FusedData f;
    f.fusedSpeedKmh  = speedKmh;
    f.lat            = 0.0f;
    f.lon            = 0.0f;
    return f;
}

static GPSData makeGpsData() {
    GPSData g;
    g.rawSpeedKmh  = 0.0f;
    g.lat          = 0.0f;
    g.lon          = 0.0f;
    g.altitudeM    = 0.0f;
    g.headingDeg   = 0.0f;
    g.satellites   = 0;
    g.hdop         = 99.90f;
    g.isStationary = true;
    return g;
}

static IMUData makeImuData() {
    IMUData i;
    i.accelX = -0.267f;
    i.accelY = -0.382f;
    i.accelZ = -0.083f;
    return i;
}

// ---- Global instances ----
TransportManager mgr;
String           deviceId;
char             telBuf[512];
char             violBuf[256];

// ============================================================
// helpers
// ============================================================
static void separator(const char* title) {
    Serial.println();
    Serial.println("========================================================");
    Serial.printf ("  %s\n", title);
    Serial.println("========================================================");
}

static void pass(const char* msg) {
    Serial.printf("[PASS] %s\n", msg);
}

static void fail(const char* msg) {
    Serial.printf("[FAIL] %s  <-- INVESTIGATE\n", msg);
}

static void checkState(TransportState expected, const char* label) {
    TransportState actual = mgr.state();
    if (actual == expected) {
        pass(label);
    } else {
        const char* got =
            actual == TransportState::WIFI_ACTIVE ? "WIFI_ACTIVE" :
            actual == TransportState::BLE_ACTIVE  ? "BLE_ACTIVE"  : "OFFLINE";
        Serial.printf("[FAIL] %s: expected %s, got %s\n",
                      label,
                      expected == TransportState::WIFI_ACTIVE ? "WIFI_ACTIVE" :
                      expected == TransportState::BLE_ACTIVE  ? "BLE_ACTIVE"  : "OFFLINE",
                      got);
    }
}

// Builds fresh telemetry JSON into telBuf using fake sensor data.
// speedKmh lets us simulate a violation scenario.
static void buildTelJson(float speedKmh = 0.0f) {
    FusedData f = makeFusedData(speedKmh);
    GPSData   g = makeGpsData();
    IMUData   i = makeImuData();
    int       limit = Storage.getSpeedLimit();

    size_t n = DataBuilder::buildTelemetry(
        deviceId, f, g, i,
        limit, 0, 0,
        telBuf, sizeof(telBuf));

    if (n == 0) Serial.println("[E][TEST] DataBuilder::buildTelemetry returned 0: buffer overflow?");
}

// Builds a violation JSON into violBuf.
// Uses 95 km/h in an 80 zone to match Phase 4.5A fixture.
static void buildViolJson() {
    FusedData f = makeFusedData(95.0f);
    f.lat = 30.604f;
    f.lon = 32.258f;

    size_t n = DataBuilder::buildViolation(
        deviceId, f,
        80.0f,    // speed limit
        5,        // duration_sec
        violBuf, sizeof(violBuf));

    if (n == 0) Serial.println("[E][TEST] DataBuilder::buildViolation returned 0");
}

// ============================================================
// setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    separator("NAZER Phase 4.5D: Full Transport Stack Test");
    Serial.println("Server must be running: uvicorn server:app --host 0.0.0.0 --port 8080");
    Serial.println("nRF Connect: scan for NAZER device to verify BLE coexistence");
    Serial.println();

    // ---- Storage init ----
    Storage.begin();
    deviceId = Storage.getDeviceId();
    Serial.printf("[SETUP] Device ID: %s\n", deviceId.c_str());

    // ---- Clear any leftover NVS records from previous test runs ----
    mgr.storage().clearAll();
    Serial.println("[SETUP] NVS cleared: starting with empty queue");
    delay(500);

    // ---- TransportManager init ----
    // begin() connects WiFi (blocking) + starts BLE advertising (non-blocking)
    separator("INIT: TransportManager.begin()");
    mgr.begin();
    delay(1000);

    checkState(TransportState::WIFI_ACTIVE,
               "State == WIFI_ACTIVE after begin()");

    Serial.printf("[INFO] WiFi IP:  %s\n", mgr.wifi().localIP().c_str());
    Serial.printf("[INFO] WiFi RSSI: %d dBm\n", mgr.wifi().rssi());
    Serial.printf("[INFO] BLE name:  %s\n", mgr.ble().deviceName().c_str());
    Serial.println("[INFO] Open nRF Connect → SCAN → confirm BLE device visible");

    delay(STEP_DELAY_MS);

    // ==============================================================
    // STEP 1: WiFi online: send telemetry + verify speed limit
    // ==============================================================
    separator("STEP 1: WiFi online: 3x telemetry POST");

    int successCount = 0;
    for (int i = 0; i < TELEMETRY_SENDS; i++) {
        buildTelJson();
        bool ok = mgr.sendTelemetry(telBuf);
        Serial.printf("[STEP1] sendTelemetry #%d → %s\n", i + 1, ok ? "OK" : "FAIL");
        if (ok) successCount++;

        // tick() manages BLE NOTIFY timer: call it between sends
        mgr.tick(telBuf);
        delay(2000);
    }

    if (successCount == TELEMETRY_SENDS) {
        pass("All 3 telemetry POSTs returned true");
    } else {
        fail("Not all telemetry POSTs succeeded: check server is running");
    }

    int limit = mgr.lastReceivedSpeedLimit();
    if (limit > 0) {
        Serial.printf("[STEP1] Server speed limit: %d km/h\n", limit);
        pass("Speed limit received from server");
    } else {
        fail("Speed limit not received: check server response JSON");
    }

    // Verify BLE coexistence: WiFi must still be up
    checkState(TransportState::WIFI_ACTIVE, "State still WIFI_ACTIVE after 3 sends");

    delay(STEP_DELAY_MS);

    // ==============================================================
    // STEP 2: Simulate WiFi loss: disconnect and queue records
    // ==============================================================
    separator("STEP 2: Disconnect WiFi: queue records offline");
    Serial.println("[ACTION] Disconnecting WiFi...");

    mgr.wifi().disconnect();

    // WiFi driver is async: poll until _updateState() confirms OFFLINE.
    // Typically settles in ~1s; 5s timeout guards against hardware stall.
    {
        uint32_t waitStart = millis();
        while (millis() - waitStart < 5000UL) {
            mgr.tick(nullptr);   // drives _updateState() on every call
            if (mgr.state() == TransportState::OFFLINE) break;
            delay(100);
        }
    }

    checkState(TransportState::OFFLINE, "State == OFFLINE after WiFi disconnect");

    Serial.printf("[STEP2] Queuing %d telemetry + %d violation records while offline...\n",
                  OFFLINE_QUEUE_TEL, OFFLINE_QUEUE_VIOL);

    // Queue violation first (should still be replayed first due to LocalStorage priority)
    buildViolJson();
    bool violQueued = mgr.sendViolation(violBuf);
    Serial.printf("[STEP2] sendViolation (offline) → %s\n", violQueued ? "QUEUED" : "FAIL");
    if (violQueued) pass("Violation queued to NVS while offline");
    else            fail("Violation not queued: LocalStorage error?");
    delay(200);  // match telemetry pacing: NVS write must complete before count read

    // Queue telemetry records
    int queuedTel = 0;
    for (int i = 0; i < OFFLINE_QUEUE_TEL; i++) {
        buildTelJson();
        bool ok = mgr.sendTelemetry(telBuf);
        Serial.printf("[STEP2] sendTelemetry (offline) #%d → %s\n",
                      i + 1, ok ? "QUEUED" : "FAIL");
        if (ok) queuedTel++;
        delay(200);
    }

    if (queuedTel == OFFLINE_QUEUE_TEL) {
        pass("All offline telemetry records queued to NVS");
    } else {
        fail("Not all offline telemetry records queued");
    }

    // Verify NVS counts
    uint8_t tCount = mgr.storage().telemetryCount();
    uint8_t vCount = mgr.storage().violationCount();
    Serial.printf("[STEP2] NVS queue: telemetry=%d  violations=%d\n", tCount, vCount);

    if (tCount == OFFLINE_QUEUE_TEL && vCount == OFFLINE_QUEUE_VIOL) {
        pass("NVS queue counts correct");
    } else {
        Serial.printf("[FAIL] Expected tel=%d viol=%d, got tel=%d viol=%d\n",
                      OFFLINE_QUEUE_TEL, OFFLINE_QUEUE_VIOL, tCount, vCount);
    }

    delay(STEP_DELAY_MS);

    // ==============================================================
    // STEP 3: Reconnect WiFi → replay queued records
    // ==============================================================
    separator("STEP 3: Reconnect WiFi: replay NVS queue");
    Serial.println("[ACTION] Reconnecting WiFi...");

    bool reconnected = mgr.wifi().connect();
    if (reconnected) {
        Serial.printf("[STEP3] WiFi reconnected: IP=%s\n",
                      mgr.wifi().localIP().c_str());
        pass("WiFi reconnected successfully");
    } else {
        fail("WiFi reconnect FAILED: remaining steps may not pass");
    }

    // Force state update + immediate replay
    // tick() with _lastReplayAttemptMs=0 triggers replayAll() immediately
    Serial.println("[STEP3] Calling tick() to trigger replay...");
    mgr.tick(nullptr);

    // Give replay time to finish (3 tel + 1 viol × ~200ms each + HTTP RTT)
    delay(8000);

    // After replay, NVS should be empty
    tCount = mgr.storage().telemetryCount();
    vCount = mgr.storage().violationCount();
    Serial.printf("[STEP3] NVS queue after replay: telemetry=%d  violations=%d\n",
                  tCount, vCount);

    if (tCount == 0 && vCount == 0) {
        pass("NVS queue empty after replay: all records delivered");
    } else {
        fail("NVS queue not empty after replay: check server is running");
    }

    checkState(TransportState::WIFI_ACTIVE, "State == WIFI_ACTIVE after reconnect");

    // Verify server received violation before telemetry
    Serial.println("[MANUAL CHECK] Server log should show:");
    Serial.println("  VIOLATION #1 appears BEFORE TELEMETRY records from replay");
    Serial.println("  (violations have priority in LocalStorage.replayAll())");

    delay(STEP_DELAY_MS);

    // ==============================================================
    // STEP 4: Continuous loop: telemetry every 10s, BLE coexistence
    // ==============================================================
    separator("STEP 4: Continuous loop (60 seconds)");
    Serial.println("[INFO] Watch for:");
    Serial.println("  - Telemetry POSTs every 10s in server log");
    Serial.println("  - BLE NOTIFY every 10s if nRF Connect is connected");
    Serial.println("  - State remains WIFI_ACTIVE throughout");
    Serial.println("  - [TRANSPORT] State: WiFi  queued: tel=0 viol=0  (every 30s)");
    Serial.println();

    uint32_t stepStart = millis();
    uint32_t lastSendMs = 0;
    int loopSends = 0;

    while (millis() - stepStart < 60000UL) {
        uint32_t now = millis();

        buildTelJson();
        mgr.tick(telBuf);  // BLE NOTIFY handled inside tick()

        if (now - lastSendMs >= 10000UL) {
            lastSendMs = now;
            bool ok = mgr.sendTelemetry(telBuf);
            loopSends++;
            Serial.printf("[STEP4] Loop send #%d → %s  state=%s\n",
                          loopSends, ok ? "OK" : "FAIL", mgr.statusLabel());
        }

        delay(100);
    }

    if (loopSends >= 5) {
        pass("Continuous loop: >= 5 telemetry sends in 60s");
    } else {
        fail("Continuous loop: fewer sends than expected");
    }

    delay(STEP_DELAY_MS);

    // ==============================================================
    // STEP 5: Speed limit priority: BLE vs WiFi
    // ==============================================================
    separator("STEP 5: Speed limit source priority");
    Serial.println("[INFO] lastReceivedSpeedLimit() should prefer WiFi (server) over BLE");
    Serial.println("[INFO] To test BLE path: write {\"cmd\":\"set_limit\",\"value\":90}");
    Serial.println("       to characteristic 1c95d5e3-... in nRF Connect");
    Serial.println("       Then watch Serial for 'Speed limit set to 90'");
    Serial.println();

    // WiFi limit (from last server response: should be non-zero by now)
    int wifiLimit = mgr.wifi().lastReceivedSpeedLimit();
    int bleLimit  = mgr.ble().lastReceivedSpeedLimit();  // 0 unless BLE write happened
    int combined  = mgr.lastReceivedSpeedLimit();

    Serial.printf("[STEP5] WiFi speed limit:  %d km/h\n", wifiLimit);
    Serial.printf("[STEP5] BLE speed limit:   %d km/h  (0 = no write received yet)\n", bleLimit);
    Serial.printf("[STEP5] Combined (mgr):    %d km/h  (WiFi wins if > 0)\n", combined);

    if (wifiLimit > 0 && combined == wifiLimit) {
        pass("WiFi limit takes priority when > 0");
    } else if (wifiLimit == 0 && bleLimit > 0 && combined == bleLimit) {
        pass("BLE limit used as fallback (no WiFi limit received)");
    } else if (wifiLimit == 0 && bleLimit == 0) {
        Serial.println("[INFO] No speed limit received from either transport yet: normal if server not configured");
    } else {
        fail("Speed limit priority logic unexpected result");
    }

    // ==============================================================
    // Summary
    // ==============================================================
    separator("Phase 4.5D: Test Complete");
    Serial.println("Review PASS/FAIL lines above.");
    Serial.println();
    Serial.println("Pass criteria checklist:");
    Serial.println("  [ ] BOOT:   State = WIFI_ACTIVE on startup");
    Serial.println("  [ ] STEP 1: 3x telemetry POSTs succeeded");
    Serial.println("  [ ] STEP 1: Speed limit received from server");
    Serial.println("  [ ] STEP 1: BLE device visible in nRF Connect scan");
    Serial.println("  [ ] STEP 2: State → OFFLINE after WiFi disconnect");
    Serial.println("  [ ] STEP 2: 3 telemetry + 1 violation queued to NVS");
    Serial.println("  [ ] STEP 3: WiFi reconnected successfully");
    Serial.println("  [ ] STEP 3: Violation replayed BEFORE telemetry (check server log)");
    Serial.println("  [ ] STEP 3: NVS empty after replay");
    Serial.println("  [ ] STEP 4: Continuous sends + BLE NOTIFY coexist for 60s");
    Serial.println("  [ ] STEP 5: Speed limit priority correct (WiFi > BLE)");
    Serial.println();
    Serial.println("If all pass → update NAZER_SESSION_SUMMARY.md: 4.5D DONE");
    Serial.println("Next phase: 5: GSM Transport (SIM800L)");
}

// ============================================================
// loop(): idle after setup() completes the full test sequence
// ============================================================
void loop() {
    // Keep BLE alive and log state every 30s so you can see it's still running
    static uint32_t lastLog = 0;
    uint32_t now = millis();

    if (now - lastLog >= 30000UL) {
        lastLog = now;
        Serial.printf("[IDLE] State: %s  BLE: %s  queued: tel=%d viol=%d\n",
                      mgr.statusLabel(),
                      mgr.ble().isConnected() ? "client connected" : "advertising",
                      mgr.storage().telemetryCount(),
                      mgr.storage().violationCount());
    }

    // Keep BLE NOTIFY running if a client connects during idle
    static char idleBuf[512];
    FusedData f = makeFusedData();
    GPSData   g = makeGpsData();
    IMUData   i = makeImuData();
    DataBuilder::buildTelemetry(deviceId, f, g, i,
                                Storage.getSpeedLimit(), 0, 0,
                                idleBuf, sizeof(idleBuf));
    mgr.tick(idleBuf);

    delay(100);
}