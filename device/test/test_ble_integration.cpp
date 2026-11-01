// =============================================================
// test/test_ble_integration.cpp
// NAZER: Self-Running BLE Integration Test
//
// PURPOSE:
//   Full end-to-end integration test firmware.
//   No GPS, IMU, or GSM hardware required.
//   Boots, advertises BLE as "NAZER-EFD0", and automatically
//   cycles through realistic driving scenarios so the Flutter
//   app can be tested against real BLE data.
//
// WHAT IT DOES:
//   - Advertises as "NAZER-EFD0" with correct service UUID
//   - Sends telemetry NOTIFY every 10 seconds (all real fields)
//   - Auto-cycles speed through: idle → accel → cruise → overspeed → brake → stop
//   - Fires violation NOTIFY when speed > limit + 5 for 3+ seconds
//   - Accepts {"cmd":"set_limit","value":N} WRITE from app
//   - Shows BLE status + current scenario on OLED
//   - Full Serial logging at 115200
//
// HOW TO USE:
//   In platformio.ini, set:
//     build_src_filter = -<*> +<../test/test_ble_integration.cpp>
//   Flash, open Serial Monitor at 115200.
//   Open Flutter app → it will auto-find and connect to the device.
//
// MOCK SCENARIO LOOP (~120 seconds total):
//   0–10s   IDLE         speed = 0           stationary = true
//   10–25s  ACCELERATE   speed 0→60          stationary flips at 2 km/h
//   25–50s  CRUISE       speed = 60          at limit, no violation
//   50–70s  OVERSPEED    speed 60→85→80      violation fires ~3s after 65 km/h crossed
//   70–85s  BRAKE        speed 85→30
//   85–100s CRUISE LOW   speed = 30
//   100–115s ACCELERATE  speed 30→70
//   115–120s STOP        speed 70→0          stationary
//   → loops
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

#include "config.h"

// ---- OLED (optional: gracefully skipped if not connected) ----
#include <SH1106Wire.h>
static SH1106Wire oled(OLED_I2C_ADDR, SDA_PIN, SCL_PIN);
static bool oledOk = false;

// ---- BLE UUIDs (from BLETransport.h) ----
#define SVC_UUID   "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define TEL_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CMD_UUID   "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"
#define CHUNK_SIZE 200

// ---- Device identity ----
// Hardcoded to match the verified device name from the session summary.
// In production firmware this is derived from MAC via _makeDeviceName().
#define DEVICE_NAME "NAZER-EFD0"
#define DEVICE_ID   "NAZER_0B49EFD0"

// ---- Mock GPS anchor (Cairo area: same region as real Phase 2 fix) ----
#define BASE_LAT  30.604000
#define BASE_LON  32.258000

// =============================================================
// SCENARIO DEFINITION
// =============================================================
struct ScenarioStep {
    const char* label;      // shown on OLED + Serial
    float       targetSpeed;// km/h
    float       rampRate;   // km/h per second (0 = instant snap)
    uint32_t    durationMs; // how long to hold this step
};

static const ScenarioStep SCENARIO[] = {
    { "IDLE",        0.0f,  0.0f,  10000 },   // parked
    { "ACCEL",      60.0f,  4.0f,  15000 },   // 0 → 60 km/h
    { "CRUISE 60",  60.0f,  0.0f,  25000 },   // hold 60
    { "OVERSPEED",  85.0f,  3.0f,  20000 },   // 60 → 85: crosses 65 km/h limit+5
    { "BRAKE",      30.0f,  5.0f,  15000 },   // 85 → 30
    { "CRUISE 30",  30.0f,  0.0f,  15000 },   // hold 30
    { "ACCEL 70",   70.0f,  4.0f,  15000 },   // 30 → 70: slightly over limit
    { "STOP",        0.0f,  6.0f,  15000 },   // 70 → 0
};
static const uint8_t SCENARIO_COUNT = sizeof(SCENARIO) / sizeof(SCENARIO[0]);

// =============================================================
// STATE
// =============================================================
static int     g_speedLimit    = 60;   // km/h: updated by app SET_LIMIT command
static float   g_mockSpeed     = 0.0f; // current mock speed km/h
static bool    g_stationary    = true;

// Scenario runner
static uint8_t  g_scenarioIdx   = 0;
static uint32_t g_stepStartMs   = 0;

// Violation tracking
static bool     g_inViolation   = false;
static uint32_t g_violStartMs   = 0;
static bool     g_violFired     = false; // only fire once per violation window

// Telemetry timer
static uint32_t g_lastTelMs     = 0;

// Mock accel value (small noise around 0 when cruising, non-zero when ramping)
static float    g_mockAccelX    = 0.0f;

// Mock GPS wander (tiny drift to simulate movement)
static double   g_lat           = BASE_LAT;
static double   g_lon           = BASE_LON;
static uint8_t  g_sats          = 8;
static float    g_hdop          = 1.20f;
static float    g_heading       = 45.0f;   // NE
static float    g_altitude      = 12.5f;
static uint8_t  g_battery       = 87;
static uint8_t  g_gsmSignal     = 22;      // CSQ ≈ good

// Counters for Serial reporting
static uint32_t g_telCount      = 0;
static uint32_t g_violCount     = 0;

// =============================================================
// BLE OBJECTS
// =============================================================
static BLEServer*          g_server    = nullptr;
static BLECharacteristic*  g_telChar   = nullptr;
static BLECharacteristic*  g_cmdChar   = nullptr;
static bool                g_bleClient = false;

// ---- Server callbacks ----
class ServerCB : public BLEServerCallbacks {
    void onConnect(BLEServer*)    override {
        g_bleClient = true;
        Serial.println("[BLE] Client connected");
        digitalWrite(STATUS_LED_PIN, HIGH);
    }
    void onDisconnect(BLEServer*) override {
        g_bleClient = false;
        Serial.println("[BLE] Client disconnected: restarting advertising");
        digitalWrite(STATUS_LED_PIN, LOW);
        BLEDevice::startAdvertising();
    }
};
static ServerCB g_serverCB;

// ---- Command callbacks ----
class CommandCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String val = c->getValue().c_str();
        if (val.length() == 0) return;

        Serial.printf("[BLE] Command received: %s\n", val.c_str());

        JsonDocument doc;
        if (deserializeJson(doc, val) != DeserializationError::Ok) {
            Serial.println("[BLE] Bad command JSON");
            return;
        }

        const char* cmd = doc["cmd"] | "";
        if (strcmp(cmd, "set_limit") == 0) {
            int v = doc["value"] | 0;
            if (v >= 10 && v <= 200) {
                g_speedLimit = v;
                Serial.printf("[BLE] Speed limit updated → %d km/h\n", v);
            }
        } else {
            Serial.printf("[BLE] Unknown command: %s\n", cmd);
        }
    }
};
static CommandCB g_cmdCB;

// =============================================================
// HELPERS
// =============================================================

// Timestamp string: millis-based pseudo-ISO8601 (same as DataBuilder)
static void makeTimestamp(char* buf, size_t len) {
    uint32_t ms  = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    sec %= 60; min %= 60; hr %= 24;
    snprintf(buf, len, "1970-01-01T%02lu:%02lu:%02lu.%03luZ",
             (unsigned long)hr, (unsigned long)min,
             (unsigned long)sec, (unsigned long)(ms % 1000));
}

// Send a JSON string via BLE NOTIFY: chunked exactly like BLETransport.cpp
static void notifyChunked(const char* json) {
    if (!g_bleClient || !g_telChar) return;

    size_t  totalLen    = strlen(json);
    uint8_t totalChunks = (uint8_t)((totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE);

    for (uint8_t i = 0; i < totalChunks; i++) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%d/%d|", i + 1, totalChunks);
        size_t prefixLen = strlen(prefix);

        char chunk[CHUNK_SIZE + 9];
        strncpy(chunk, prefix, prefixLen);
        size_t offset    = i * CHUNK_SIZE;
        size_t chunkData = min((size_t)CHUNK_SIZE, totalLen - offset);
        memcpy(chunk + prefixLen, json + offset, chunkData);
        chunk[prefixLen + chunkData] = '\0';

        g_telChar->setValue((uint8_t*)chunk, prefixLen + chunkData);
        g_telChar->notify();

        if (i < totalChunks - 1) delay(20);
    }
}

// Build and send telemetry JSON
static void sendTelemetry() {
    char ts[32];
    makeTimestamp(ts, sizeof(ts));

    JsonDocument doc;
    doc["device_id"]       = DEVICE_ID;
    doc["timestamp"]       = ts;
    doc["lat"]             = serialized(String(g_lat, 6));
    doc["lon"]             = serialized(String(g_lon, 6));
    doc["speed_kmh"]       = serialized(String(g_mockSpeed, 1));
    doc["speed_gps_kmh"]   = serialized(String(g_mockSpeed, 1));
    doc["speed_imu_kmh"]   = serialized(String(g_mockSpeed, 1));
    doc["heading_deg"]     = serialized(String(g_heading, 1));
    doc["speed_limit_kmh"] = g_speedLimit;
    doc["altitude_m"]      = serialized(String(g_altitude, 1));
    doc["satellites"]      = g_sats;
    doc["hdop"]            = serialized(String(g_hdop, 2));
    doc["battery_pct"]     = g_battery;
    doc["gsm_signal"]      = g_gsmSignal;
    doc["is_stationary"]   = g_stationary;
    doc["accel_x"]         = serialized(String(g_mockAccelX, 4));
    doc["accel_y"]         = serialized(String(0.0012f,      4));
    doc["accel_z"]         = serialized(String(0.0008f,      4));

    char buf[512];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0) {
        Serial.println("[TEL] JSON serialisation failed");
        return;
    }

    g_telCount++;
    Serial.printf("[TEL #%lu] speed=%.1f km/h  limit=%d  stationary=%s  client=%s\n",
                  (unsigned long)g_telCount,
                  g_mockSpeed, g_speedLimit,
                  g_stationary ? "true" : "false",
                  g_bleClient  ? "yes"  : "no");
    Serial.printf("         JSON (%u bytes): %s\n", (unsigned)len, buf);

    notifyChunked(buf);
}

// Build and send violation JSON
static void sendViolation(float speed, uint32_t durationSec) {
    char ts[32];
    makeTimestamp(ts, sizeof(ts));

    JsonDocument doc;
    doc["device_id"]       = DEVICE_ID;
    doc["timestamp"]       = ts;
    doc["lat"]             = serialized(String(g_lat, 6));
    doc["lon"]             = serialized(String(g_lon, 6));
    doc["speed_kmh"]       = serialized(String(speed, 1));
    doc["speed_limit_kmh"] = serialized(String((float)g_speedLimit, 1));
    doc["duration_sec"]    = durationSec;
    // Include violation_id so BleService routes this to ViolationData
    char vid[40];
    snprintf(vid, sizeof(vid), "%s_%lu", DEVICE_ID, (unsigned long)(millis() / 1000));
    doc["violation_id"]    = vid;

    char buf[384];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0) {
        Serial.println("[VIOL] JSON serialisation failed");
        return;
    }

    g_violCount++;
    float excess = speed - g_speedLimit;
    Serial.printf("[VIOL #%lu] speed=%.1f  limit=%d  excess=+%.1f  duration=%lus\n",
                  (unsigned long)g_violCount,
                  speed, g_speedLimit, excess, (unsigned long)durationSec);
    Serial.printf("           JSON (%u bytes): %s\n", (unsigned)len, buf);

    notifyChunked(buf);
}

// =============================================================
// SCENARIO RUNNER
// =============================================================
static void updateScenario() {
    uint32_t now     = millis();
    uint32_t elapsed = now - g_stepStartMs;

    const ScenarioStep& step = SCENARIO[g_scenarioIdx];

    // ---- Advance to next step if duration elapsed ----
    if (elapsed >= step.durationMs) {
        g_scenarioIdx = (g_scenarioIdx + 1) % SCENARIO_COUNT;
        g_stepStartMs = now;
        g_violFired   = false; // reset violation flag for new step
        Serial.printf("[SCENARIO] → Step %d: %s  target=%.1f km/h\n",
                      g_scenarioIdx,
                      SCENARIO[g_scenarioIdx].label,
                      SCENARIO[g_scenarioIdx].targetSpeed);
        return;
    }

    // ---- Ramp speed toward target ----
    float target = step.targetSpeed;
    if (step.rampRate > 0.0f) {
        float dtSec = 0.1f; // called every 100ms loop
        float delta = step.rampRate * dtSec;
        if (g_mockSpeed < target) {
            g_mockSpeed = min(g_mockSpeed + delta, target);
            g_mockAccelX = step.rampRate * 0.278f; // km/h/s → m/s²
        } else if (g_mockSpeed > target) {
            g_mockSpeed = max(g_mockSpeed - delta, target);
            g_mockAccelX = -(step.rampRate * 0.278f);
        } else {
            g_mockAccelX = 0.0f;
        }
    } else {
        // Instant snap (used for IDLE step)
        g_mockSpeed  = target;
        g_mockAccelX = 0.0f;
    }

    // ---- Stationary detection (mirrors GPS.cpp hysteresis) ----
    if (g_stationary) {
        if (g_mockSpeed > MOVING_THRESHOLD_KMH)    g_stationary = false;
    } else {
        if (g_mockSpeed < STATIONARY_THRESHOLD_KMH) g_stationary = true;
    }

    // ---- Mock GPS wander (tiny drift when moving) ----
    if (!g_stationary) {
        float headingRad = g_heading * DEG_TO_RAD;
        double speedMs   = g_mockSpeed / 3.6;
        // 0.1s tick: distance = speed * dt / earth_radius_deg
        g_lat += (speedMs * 0.1 * cos(headingRad)) / 111320.0;
        g_lon += (speedMs * 0.1 * sin(headingRad)) / (111320.0 * cos(g_lat * DEG_TO_RAD));
    }

    // ---- Battery drain simulation (slow) ----
    static uint32_t lastBattMs = 0;
    if (now - lastBattMs > 60000 && g_battery > 5) { // drain 1% per minute
        g_battery--;
        lastBattMs = now;
    }
}

// =============================================================
// VIOLATION STATE MACHINE
// =============================================================
static void updateViolation() {
    bool overLimit = (g_mockSpeed > (float)g_speedLimit + VIOLATION_MARGIN_KMH);

    if (overLimit && !g_inViolation) {
        g_inViolation = true;
        g_violStartMs = millis();
        g_violFired   = false;
        Serial.printf("[VIOL] Violation window OPEN: speed=%.1f limit=%d\n",
                      g_mockSpeed, g_speedLimit);
    }

    if (g_inViolation && !g_violFired) {
        uint32_t durationMs = millis() - g_violStartMs;
        // Fire after VIOLATION_MIN_DURATION_S seconds (from config.h)
        if (durationMs >= (uint32_t)VIOLATION_MIN_DURATION_S * 1000) {
            sendViolation(g_mockSpeed, durationMs / 1000);
            g_violFired = true;
        }
    }

    if (!overLimit && g_inViolation) {
        g_inViolation = false;
        Serial.printf("[VIOL] Violation window CLOSED: duration=%lums\n",
                      (unsigned long)(millis() - g_violStartMs));
    }
}

// =============================================================
// OLED UPDATE
// =============================================================
static void updateOled() {
    if (!oledOk) return;

    oled.clear();
    oled.setFont(ArialMT_Plain_10);

    // Line 1: device name + BLE status
    char line1[32];
    snprintf(line1, sizeof(line1), "%s  BLE:%s",
             DEVICE_NAME, g_bleClient ? "CONN" : "wait");
    oled.drawString(0, 0, line1);

    // Line 2: scenario label
    oled.drawString(0, 12, SCENARIO[g_scenarioIdx].label);

    // Line 3: speed + limit
    char line3[32];
    snprintf(line3, sizeof(line3), "%.0f / %d km/h",
             g_mockSpeed, g_speedLimit);
    oled.setFont(ArialMT_Plain_16);
    oled.drawString(0, 24, line3);

    // Line 4: violation indicator + battery
    oled.setFont(ArialMT_Plain_10);
    char line4[32];
    snprintf(line4, sizeof(line4), "%s  BAT:%d%%  TEL:%lu",
             g_inViolation ? "!! VIOL !!" : "ok",
             g_battery, (unsigned long)g_telCount);
    oled.drawString(0, 44, line4);

    // Line 5: lat/lon
    char line5[32];
    snprintf(line5, sizeof(line5), "%.4f, %.4f", g_lat, g_lon);
    oled.drawString(0, 55, line5);

    oled.display();
}

// =============================================================
// BLE INIT
// =============================================================
static void initBLE() {
    Serial.printf("[BLE] Initialising as \"%s\"\n", DEVICE_NAME);

    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setMTU(247);

    g_server = BLEDevice::createServer();
    g_server->setCallbacks(&g_serverCB);

    BLEService* svc = g_server->createService(SVC_UUID);

    // Telemetry characteristic: NOTIFY + READ
    g_telChar = svc->createCharacteristic(
        TEL_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    g_telChar->addDescriptor(new BLE2902());

    // Command characteristic: WRITE
    g_cmdChar = svc->createCharacteristic(
        CMD_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    g_cmdChar->setCallbacks(&g_cmdCB);

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SVC_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.println("[BLE] Advertising started");
    Serial.printf("[BLE] Service UUID:   %s\n", SVC_UUID);
    Serial.printf("[BLE] Telemetry UUID: %s  (NOTIFY)\n", TEL_UUID);
    Serial.printf("[BLE] Command UUID:   %s  (WRITE)\n", CMD_UUID);
}

// =============================================================
// SETUP
// =============================================================
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n========================================");
    Serial.println("  NAZER BLE Integration Test");
    Serial.println("  Self-running mock data sender");
    Serial.println("========================================");
    Serial.printf("  Device: %s\n", DEVICE_NAME);
    Serial.printf("  Speed limit: %d km/h\n", g_speedLimit);
    Serial.printf("  Violation margin: %.0f km/h\n", VIOLATION_MARGIN_KMH);
    Serial.printf("  Violation min duration: %d s\n", VIOLATION_MIN_DURATION_S);
    Serial.printf("  Telemetry interval: %d ms\n", TELEMETRY_INTERVAL_MS);
    Serial.println("========================================\n");

    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // OLED: optional, continue if not found
    Wire.begin(SDA_PIN, SCL_PIN);
    oled.init();
    oled.flipScreenVertically();
    oled.setFont(ArialMT_Plain_10);
    oled.clear();
    oled.drawString(0, 0, "NAZER BLE Test");
    oled.drawString(0, 14, "Starting...");
    oled.display();
    oledOk = true;
    Serial.println("[OLED] Display initialised");

    initBLE();

    g_stepStartMs  = millis();
    g_lastTelMs    = millis();

    Serial.println("\n[MAIN] Setup complete: scenario loop starting");
    Serial.println("[MAIN] Waiting for Flutter app to connect...\n");

    // Print scenario table
    Serial.println("[SCENARIO] Full sequence:");
    uint32_t cumMs = 0;
    for (uint8_t i = 0; i < SCENARIO_COUNT; i++) {
        Serial.printf("  Step %d  t=+%lus  %-14s  target=%.0f km/h  ramp=%.1f km/h/s\n",
                      i, (unsigned long)(cumMs / 1000),
                      SCENARIO[i].label,
                      SCENARIO[i].targetSpeed,
                      SCENARIO[i].rampRate);
        cumMs += SCENARIO[i].durationMs;
    }
    Serial.printf("  [loop repeats every %lu seconds]\n\n", (unsigned long)(cumMs / 1000));
}

// =============================================================
// LOOP
// =============================================================
void loop() {
    uint32_t now = millis();

    // ---- Update mock scenario (runs every 100ms) ----
    static uint32_t lastScenarioMs = 0;
    if (now - lastScenarioMs >= 100) {
        lastScenarioMs = now;
        updateScenario();
        updateViolation();
    }

    // ---- Send telemetry every 10 seconds ----
    if (now - g_lastTelMs >= TELEMETRY_INTERVAL_MS) {
        g_lastTelMs = now;
        sendTelemetry();
    }

    // ---- Update OLED every 500ms ----
    static uint32_t lastOledMs = 0;
    if (now - lastOledMs >= 500) {
        lastOledMs = now;
        updateOled();
    }

    // ---- Heartbeat LED blink when not connected (slow: 1Hz) ----
    if (!g_bleClient) {
        static uint32_t lastBlinkMs = 0;
        if (now - lastBlinkMs >= 1000) {
            lastBlinkMs = now;
            digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        }
    }

    delay(10); // prevent WDT reset, yield to BLE stack
}