// ============================================================
// test/test_gsm.cpp: Phase 5: GSM Transport Test
//
// BEFORE FLASHING:
//   1. include/config.h:
//        #define GSM_APN  "internet.vodafone.net"   ← Vodafone EG (your SIM)
//        #define SERVER_HOST  "http://192.168.1.101:8080"  ← laptop IP
//   2. platformio.ini:
//        build_src_filter = -<*> +<../test/test_gsm.cpp>
//   3. Start laptop server:
//        uvicorn server:app --host 0.0.0.0 --port 8080
//   4. Flash → Serial Monitor 115200 baud
//
// Hardware checklist:
//   [ ] MT3608 trimpot = 4.0V measured under load with multimeter
//   [ ] 1000uF cap soldered directly at SIM800L VCC+GND pins
//   [ ] External antenna on SIM800L IPEX connector
//   [ ] SIM card inserted: Vodafone EG: data plan active
//   [ ] GPIO16 = ESP32 RX  ←  SIM800L TxD
//   [ ] GPIO17 = ESP32 TX  →  SIM800L RxD
//   [ ] GPIO25 = RST  (idle HIGH)
//   [ ] GPIO33 = PWRKEY (idle HIGH)
//
// PASS CRITERIA (update session summary when all green):
//   [ ] STEP 1: AT→OK, +CPIN:READY, CSQ >= 5
//   [ ] STEP 2: GPRS attaches with APN, bearer IP assigned
//   [ ] STEP 3: POST /v1/device/telemetry → 200 on server log
//   [ ] STEP 4: POST /v1/device/violation → 200 on server log
//   [ ] STEP 5: disconnect→queue NVS→reconnect→replayAll→server shows
//               violation BEFORE telemetry in log
// ============================================================

#include <Arduino.h>
#include "config.h"

// Sensor struct headers (needed to call DataBuilder)
#include "SensorFusion.h"   // FusedData
#include "GPS.h"            // GPSData
#include "IMU.h"            // IMUData

// Storage / data layer
#include "DataBuilder.h"
#include "LocalStorage.h"
#include "Storage.h"        // extern StorageManager Storage

// Transport
#include "GSM.h"
#include "CloudClient.h"
#include "GSMTransport.h"

// ---- Logging helpers ---------------------------------------------------
#define PASS(msg)    Serial.printf("[PASS] %s\n",   msg)
#define FAIL(msg)    Serial.printf("[FAIL] %s\n",   msg)
#define INFO(msg)    Serial.printf("[INFO] %s\n",   msg)
#define STEP(n, msg) Serial.printf("\n========== STEP %d: %s ==========\n", n, msg)
#define HLINE        Serial.println("--------------------------------------------------")

// ---- Fake sensor data helpers -----------------------------------------
// GPS no-fix indoors: identical baseline to Phase 4.5A.

static FusedData makeFused(float speedKmh = 0.0f) {
    FusedData f;
    memset(&f, 0, sizeof(f));
    f.fusedSpeedKmh = speedKmh;
    return f;
}

static GPSData makeGPS() {
    GPSData g;
    memset(&g, 0, sizeof(g));
    g.lat          = 0.0;
    g.lon          = 0.0;
    g.rawSpeedKmh  = 0.0f;
    g.hdop         = 99.9f;
    g.satellites = 0;
    g.hasFix     = false;
    return g;
}

static IMUData makeIMU() {
    IMUData i;
    memset(&i, 0, sizeof(i));
    i.accelX = -0.267f;   // same values as Phase 4.5A verified log
    i.accelY = -0.382f;
    i.accelZ = -0.083f;
    return i;
}

// ---- Globals -----------------------------------------------------------
static GSMTransport gsmTransport;
static LocalStorage localStorage;

// -----------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n============================================================");
    Serial.println("  NAZER Phase 5: GSM Transport Test");
    Serial.printf ("  APN: %s\n",    GSM_APN);
    Serial.printf ("  Server: %s\n", SERVER_HOST);
    Serial.println("============================================================\n");

    // ---- NVS init ----
    Storage.begin();
    String deviceId  = Storage.getDeviceId();
    int    speedLimit = Storage.getSpeedLimit();
    Serial.printf("[SETUP] Device ID: %s   Speed limit: %d km/h\n\n",
                  deviceId.c_str(), speedLimit);

    localStorage.begin();

    // ==================================================================
    // STEP 1: AT handshake + SIM ready + signal quality
    // ==================================================================
    STEP(1, "AT handshake + SIM ready + signal quality");

    // Use a raw GSM object for diagnostics before GSMTransport::connect()
    // This lets us report exact failure point without the retry logic.
    GSM diagGsm;
    diagGsm.begin();
    diagGsm.sendAT("ATE0", "OK", 1000);

    if (diagGsm.sendAT("AT", "OK", 2000)) {
        PASS("AT → OK");
    } else {
        FAIL("AT → no response");
        INFO("Check: UART2 wiring, 4.0V on MT3608, 1000uF cap at SIM800L VCC");
        INFO("Halting: fix hardware then re-flash");
        while (true) delay(1000);
    }

    if (diagGsm.simReady()) {
        PASS("+CPIN: READY: SIM present and unlocked");
    } else {
        // Not halting: SIM may still need a few seconds
        FAIL("+CPIN not READY: check SIM card insertion and PIN lock");
    }

    int csq = diagGsm.signalQuality();
    if      (csq == 99)           FAIL("CSQ=99: no network detected: check antenna");
    else if (csq < GSM_CSQ_MIN)   Serial.printf("[FAIL] CSQ=%d: too weak (min=%d): check antenna\n", csq, GSM_CSQ_MIN);
    else if (csq < GSM_CSQ_GOOD)  Serial.printf("[WARN] CSQ=%d: fair signal: proceeding\n",           csq);
    else                          Serial.printf("[PASS] CSQ=%d: good signal\n",                         csq);
    HLINE;

    // ==================================================================
    // STEP 2: GPRS attach
    // ==================================================================
    STEP(2, "GPRS attach: APN: " GSM_APN);

    if (gsmTransport.connect()) {
        PASS("GSMTransport::connect(): GPRS attached, bearer open");
        Serial.printf("[INFO] Post-connect CSQ: %d\n", gsmTransport.lastSignalQuality());
    } else {
        FAIL("GSMTransport::connect() failed");
        INFO("Check: APN value, SIM data plan balance, antenna, 4.0V supply");
        INFO("Halting: fix then re-flash");
        while (true) delay(1000);
    }
    HLINE;

    // ==================================================================
    // STEP 3: POST /v1/device/telemetry → 200 OK
    // ==================================================================
    STEP(3, "POST /v1/device/telemetry");

    {
        FusedData fused = makeFused();
        GPSData   gps   = makeGPS();
        IMUData   imu   = makeIMU();

        char body[512];
        size_t n = DataBuilder::buildTelemetry(
            deviceId, fused, gps, imu,
            speedLimit,  // current speed limit from NVS
            0,           // battPct: ADC not wired yet
            0,           // gsmSignal: will be real in Phase 7
            body, sizeof(body)
        );

        if (n == 0) {
            FAIL("DataBuilder::buildTelemetry() returned 0: JSON too large");
        } else {
            Serial.printf("[INFO] Telemetry JSON (%u bytes): %s\n", (unsigned)n, body);

            bool ok = gsmTransport.sendTelemetry(body);
            if (ok) {
                PASS("Telemetry POST → 200 OK");
                Serial.printf("[INFO] Server speed limit: %d km/h\n",
                              gsmTransport.lastReceivedSpeedLimit());
            } else {
                FAIL("Telemetry POST failed: verify SERVER_HOST in config.h and server is running");
            }
        }
    }
    HLINE;

    // ==================================================================
    // STEP 4: POST /v1/device/violation → 200 OK
    // ==================================================================
    STEP(4, "POST /v1/device/violation: 95 km/h in 80 zone");

    {
        FusedData fused = makeFused(95.0f);   // fused speed = 95 km/h

        char body[512];
        size_t n = DataBuilder::buildViolation(
            deviceId,
            fused,
            80.0f,    // speed limit
            4,        // durationSec: 4s over limit (> VIOLATION_MIN_DURATION_S=3)
            body, sizeof(body)
        );

        if (n == 0) {
            FAIL("DataBuilder::buildViolation() returned 0");
        } else {
            Serial.printf("[INFO] Violation JSON (%u bytes): %s\n", (unsigned)n, body);

            bool ok = gsmTransport.sendViolation(body);
            if (ok) {
                PASS("Violation POST → 200 OK");
                Serial.printf("[INFO] Violation ID: %s\n",
                              gsmTransport.lastViolationId().c_str());
            } else {
                FAIL("Violation POST failed");
            }
        }
    }
    HLINE;

    // ==================================================================
    // STEP 5: Disconnect → queue to NVS → reconnect → replayAll
    // ==================================================================
    STEP(5, "Offline queue + replay");

    // 5a. Queue 2 telemetry + 1 violation as JSON strings into NVS
    INFO("Queuing 2 telemetry + 1 violation to LocalStorage...");
    {
        FusedData fused = makeFused();
        GPSData   gps   = makeGPS();
        IMUData   imu   = makeIMU();

        char tel1[512], tel2[512], viol[512];

        DataBuilder::buildTelemetry(deviceId, fused, gps, imu,
                                    speedLimit, 0, 0, tel1, sizeof(tel1));
        DataBuilder::buildTelemetry(deviceId, fused, gps, imu,
                                    speedLimit, 0, 0, tel2, sizeof(tel2));
        DataBuilder::buildViolation(deviceId, makeFused(95.0f),
                                    80.0f, 4, viol, sizeof(viol));

        localStorage.enqueueTelemetry(tel1);
        localStorage.enqueueTelemetry(tel2);
        localStorage.enqueueViolation(viol);
    }

    {
        int tel = localStorage.telemetryCount();
        int vio = localStorage.violationCount();
        Serial.printf("[INFO] NVS queue: telemetry=%d  violations=%d\n", tel, vio);
        if (tel == 2 && vio == 1) PASS("NVS queue correct (tel=2  viol=1)");
        else                      Serial.printf("[FAIL] NVS queue mismatch (tel=%d  viol=%d)\n", tel, vio);
    }

    // 5b. Simulate GPRS loss
    INFO("Disconnecting GPRS (simulating signal loss)...");
    gsmTransport.disconnect();
    Serial.printf("[INFO] isConnected() = %s\n",
                  gsmTransport.isConnected() ? "true (unexpected!)" : "false ✓");

    // 5c. Restore GPRS
    INFO("Restoring GPRS via reconnectNow()...");
    if (gsmTransport.reconnectNow()) {
        PASS("GPRS restored");
    } else {
        FAIL("GPRS reconnect failed: replay cannot proceed");
        INFO("Check signal + APN. Will retry in loop().");
        HLINE;
        return;
    }

    // 5d. replayAll: LocalStorage handles violation-first ordering internally
    INFO("Calling localStorage.replayAll(): violations replayed before telemetry...");
    uint8_t sent = localStorage.replayAll(&gsmTransport);
    Serial.printf("[INFO] replayAll() sent %d records\n", sent);

    if (localStorage.telemetryCount() == 0 && localStorage.violationCount() == 0) {
        PASS("NVS queue empty: all records delivered");
        INFO("Check server log: violation record MUST appear before telemetry records");
    } else {
        Serial.printf("[FAIL] NVS not empty after replay: tel=%d  viol=%d\n",
                      localStorage.telemetryCount(),
                      localStorage.violationCount());
    }
    HLINE;

    // ==================================================================
    // Final summary
    // ==================================================================
    Serial.println("\n============================================================");
    Serial.println("  Phase 5 Setup Complete: entering loop()");
    Serial.printf ("  Telemetry every %d ms via GSM\n", TELEMETRY_INTERVAL_MS);
    Serial.println("  Verify on server:");
    Serial.println("    STEP 3 → telemetry record  (fused=0.0 km/h)");
    Serial.println("    STEP 4 → violation record  (95.0 km/h in 80 zone)");
    Serial.println("    STEP 5 → replay: VIOLATION line before TELEMETRY lines");
    Serial.println("============================================================\n");
}

// -----------------------------------------------------------------------
// loop(): continuous telemetry every TELEMETRY_INTERVAL_MS
//          offline queuing when GPRS is down
// -----------------------------------------------------------------------
static uint32_t lastTelMs = 0;
static uint32_t loopCount = 0;

void loop() {
    uint32_t now = millis();

    // ---- Reconnect watchdog ----
    if (!gsmTransport.isConnected()) {
        static uint32_t lastRetryMs = 0;
        if (now - lastRetryMs >= GSM_RECONNECT_DELAY_MS) {
            lastRetryMs = now;
            Serial.println("[LOOP] OFFLINE: attempting GPRS reconnect...");
            if (gsmTransport.reconnectNow()) {
                Serial.println("[LOOP] GPRS restored: replaying NVS queue...");
                uint8_t sent = localStorage.replayAll(&gsmTransport);
                Serial.printf("[LOOP] Replay complete: %d records sent\n", sent);
            }
        }
        return;   // Don't attempt new sends while offline
    }

    // ---- Periodic telemetry ----
    if (now - lastTelMs >= TELEMETRY_INTERVAL_MS) {
        lastTelMs = now;
        loopCount++;

        String deviceId  = Storage.getDeviceId();
        int    limit     = gsmTransport.lastReceivedSpeedLimit();
        if (limit == 0) limit = Storage.getSpeedLimit();   // fallback to NVS

        FusedData fused = makeFused();
        GPSData   gps   = makeGPS();
        IMUData   imu   = makeIMU();

        char body[512];
        DataBuilder::buildTelemetry(deviceId, fused, gps, imu,
                                    limit, 0, 0, body, sizeof(body));

        bool ok = gsmTransport.sendTelemetry(body);
        Serial.printf("[LOOP #%lu] Telemetry %s: limit=%d km/h\n",
                      loopCount, ok ? "OK" : "FAIL", limit);

        if (!ok) {
            // On failure queue to NVS so we don't lose data
            localStorage.enqueueTelemetry(body);
            Serial.printf("[LOOP] Queued to NVS (tel=%d)\n",
                          localStorage.telemetryCount());
        }
    }
}