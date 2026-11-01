// NAZER v3: offline road limits, store and forward, parked power policy.
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <sys/time.h>

#include "config.h"
#include "Logger.h"
#include "SensorFusion.h"
#include "Storage.h"
#include "DataBuilder.h"
#include "TransportManager.h"
#include "VehicleConfig.h"
#include "CrashDetector.h"
#include "Cellular.h"
#include "RoadLimits.h"
#include "PowerPolicy.h"
#include "BleAuth.h"
#include "ClaimResponse.h"

static SensorFusion     g_fusion;
static TransportManager g_transport;   // BLE GATT server + offline violation queue
static CrashDetector    g_crash;
static BleAuth          g_bleAuth(BLE_PAIR_CODE);   // pairing gate for app commands

static RoadLimits g_roads;
static float g_userLimitKmh=80, g_roadLimitKmh=0;
static bool g_saver=true, g_parked=false;
static PowerPolicy::StationaryTimer g_stillTimer;
static uint32_t g_lastRecordMs=0, g_lastRoadMs=0;
static bool g_hadFix=false, g_gpsLost=false;
static uint32_t g_lastStatusMs=0;
static PowerPolicy::BatteryAlert g_batteryAlert;
static float effectiveLimit() { return g_roadLimitKmh>0 ? g_roadLimitKmh : g_userLimitKmh; }
static void setParked(bool value);
static void stateEvent(const char* kind, int battery=-1);
static void updateState(uint32_t now);
static void checkBattery(int percent);
static bool     g_inViolation = false;
static uint32_t g_violationStartMs = 0;
static bool     g_violationFired = false;

// Crash alert waiting for the grace period to run out
static bool      g_crashPending = false;
static uint32_t  g_crashAtMs = 0;
static char      g_crashEventId[48] = {};
static CrashInfo g_crashInfo = {};

// Last good position, for SMS replies when the fix is momentarily lost
static double   g_lastLat = 0, g_lastLon = 0;
static uint32_t g_lastFixMs = 0;

static char     g_telJson[896] = {};
static uint32_t g_lastTelBuildMs = 0, g_lastCloudMs = 0, g_lastClockSyncMs = 0, g_lastLedMs = 0;
static bool     g_clockSet = false;

static void handleViolation(float speedKmh);
static void handleCrash(float speedKmh);
static void handleBleCommands();
static void handleSmsCommands();
static void buildTelemetry(float speedKmh);
static void syncClockFromGps();
static String deviceId() { return Storage.getDeviceId(); }

// ---- helpers ------------------------------------------------------------

static uint32_t unixNow() {
    time_t t = time(nullptr);
    return t > 1700000000 ? (uint32_t)t : 0;
}

static void mapsLink(char* out, size_t len) {
    if (g_lastFixMs == 0) { snprintf(out, len, "no GPS fix yet"); return; }
    uint32_t ageMin = (millis() - g_lastFixMs) / 60000;
    if (ageMin == 0) snprintf(out, len, "https://maps.google.com/?q=%.6f,%.6f", g_lastLat, g_lastLon);
    else snprintf(out, len, "https://maps.google.com/?q=%.6f,%.6f (%lu min ago)", g_lastLat, g_lastLon, (unsigned long)ageMin);
}

static void notifyConfig() {
    JsonDocument doc;
    JsonObject c = doc["config"].to<JsonObject>();
    c["speed_limit_kmh"] = (int)g_userLimitKmh;
    c["saver"] = g_saver;
    c["pack_version"] = g_roads.packVersion();
    c["vehicle_mode"] = Vehicle.modeName();
    c["imu_calibrated"] = g_fusion.imu().isCalibrated();
    JsonArray arr = c["contacts"].to<JsonArray>();
    for (uint8_t i = 0; i < Vehicle.contactCount(); i++) arr.add(Vehicle.contact(i));
    c["device_id"] = deviceId();
    char buf[384];
    serializeJson(doc, buf, sizeof(buf));
    g_transport.ble().notifyJson(buf);
}

static bool setLimit(int kmh) {
    if (kmh < 10 || kmh > 200) return false;
    g_userLimitKmh = (float)kmh;
    Storage.setSpeedLimit(kmh);
    LOG_INFO("MAIN", "Speed limit set to %d km/h", kmh);
    return true;
}

// ---- setup ---------------------------------------------------------------

void setup() {
    Logger::begin(115200);   // LOG_* macros are silent until this runs
    delay(300);
    Serial.printf("\n=== NAZER firmware %s ===\n", NAZER_FW_VERSION);
    pinMode(STATUS_LED_PIN, OUTPUT);

    Wire.begin(SDA_PIN, SCL_PIN);
    Storage.begin();
    Vehicle.begin();
    g_userLimitKmh = (float)Storage.getSpeedLimit(Vehicle.defaultLimitKmh());
    g_saver=Storage.getSaver();
    g_roads.begin();
    LOG_INFO("MAIN", "Device %s, mode %s, limit %.0f km/h",
             deviceId().c_str(), Vehicle.modeName(), effectiveLimit());

    if (!g_fusion.imu().begin()) {
        LOG_ERROR("MAIN", "IMU init FAILED: speed is GPS-only and crash detection is off");
    } else if (!g_fusion.imuCalibrated()) {
        LOG_WARN("MAIN", "IMU not calibrated: send {\"cmd\":\"calibrate_imu\"} with the device level");
    }
    g_fusion.gps().begin();

    g_transport.begin();   // BLE advertising starts here
    Cellular::begin();     // modem task on core 0, returns immediately

    LOG_INFO("MAIN", "Boot complete");
}

// ---- loop ----------------------------------------------------------------

void loop() {
    uint32_t now = millis();

    g_fusion.gps().update();
    g_fusion.update();
    float speed = g_fusion.getFusedSpeedKmh();

    FusedData fused = g_fusion.getData();
    if (fused.hasFix) {
        g_lastLat = fused.lat;
        g_lastLon = fused.lon;
        g_lastFixMs = now;
    }

    updateState(now);
    handleCrash(speed);
    handleViolation(speed);
    handleBleCommands();
    handleSmsCommands();

    if (now - g_lastTelBuildMs >= BLE_LIVE_INTERVAL_MS) {
        g_lastTelBuildMs = now;
        buildTelemetry(speed);
    }
    g_transport.tick(g_telJson[0] ? g_telJson : nullptr);   // BLE notify at BLE_TELEMETRY_INTERVAL_MS

    bool clockReady=g_clockSet || now>60000;
    uint32_t uploadEvery=PowerPolicy::uploadInterval(g_parked,g_saver);
    if (!g_parked && fused.hasFix && clockReady &&
        (!g_lastRecordMs || now-g_lastRecordMs>=PowerPolicy::recordInterval(g_saver))) {
        buildTelemetry(speed);
        g_lastRecordMs=now;
        Cellular::postTelemetry(g_telJson);
    }
    // No GPS fix (indoors, garage): still report status so the app and dashboard
    // see the device online over the internet; one point per upload interval.
    if (!g_parked && !fused.hasFix && clockReady &&
        (!g_lastStatusMs || now-g_lastStatusMs>=uploadEvery)) {
        buildTelemetry(speed);
        g_lastStatusMs=now;
        Cellular::postTelemetry(g_telJson);
    }
    if (now-g_lastCloudMs>=uploadEvery) {
        if (g_parked && clockReady) {
            buildTelemetry(speed); // exactly one heartbeat; last position if GPS is lost
            Cellular::postTelemetry(g_telJson);
        }
        Cellular::requestUpload();
        g_lastCloudMs=now;
    }

    if (now - g_lastClockSyncMs >= (g_clockSet ? 600000UL : 2000UL)) {
        g_lastClockSyncMs = now;
        syncClockFromGps();
    }

    // LED: slow blink normally, fast while a crash alert is pending
    if (now - g_lastLedMs >= (g_crashPending ? 150UL : 1000UL)) {
        g_lastLedMs = now;
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    }

    delay(10);
}

// ---- telemetry -------------------------------------------------------------

static void buildTelemetry(float speedKmh) {
    CellularStatus cs = Cellular::status();
    DataBuilder::buildTelemetry(
        deviceId(), g_fusion.getData(), g_fusion.gps().getData(), g_fusion.imu().getData(),
        (int)effectiveLimit(),
        0,                                   // battery: divider not fitted on the prototype PCB
        cs.csq == 99 ? 0 : cs.csq,
        g_telJson, sizeof(g_telJson),
        Vehicle.modeName(),
        speedKmh > effectiveLimit());
    JsonDocument doc;
    if (deserializeJson(doc,g_telJson)) { g_telJson[0]=0; return; }
    if (g_roadLimitKmh>0) doc["road_limit_kmh"]=(int)g_roadLimitKmh;
    else doc["road_limit_kmh"]=nullptr;
    doc["limit_source"]=g_roadLimitKmh>0 ? "road" : "user";
    doc["power_state"]=g_parked ? "parked" : "active";
    doc["saver"]=g_saver;
    doc["gsm_ok"]=cs.modemOk && cs.registered;
    doc["queued"]=Cellular::queued();
    if (!g_fusion.hasFix()) {
        if (g_hadFix) {   // last known position, flagged by gps_fix:false
            doc["lat"]=serialized(String(g_lastLat,6));
            doc["lon"]=serialized(String(g_lastLon,6));
        } else {          // never had a fix since boot: no position, not 0,0
            doc["lat"]=nullptr;
            doc["lon"]=nullptr;
        }
    }
    if (measureJson(doc)>=sizeof(g_telJson)) {g_telJson[0]=0; return;}
    serializeJson(doc,g_telJson,sizeof(g_telJson));
    // Zero means unknown on this PCB: there is no fitted battery divider.
    checkBattery(-1);
}

static void syncClockFromGps() {
    uint32_t epoch;
    if (!g_fusion.gps().utcEpoch(epoch)) return;
    struct timeval tv = {(time_t)epoch, 0};
    settimeofday(&tv, nullptr);
    if (!g_clockSet) LOG_INFO("MAIN", "Clock set from GPS: %lu", (unsigned long)epoch);
    g_clockSet = true;
}

// ---- violations ------------------------------------------------------------
// CLEAR → window open (speed > limit + margin) → fired after N s → CLEAR

static void handleViolation(float speedKmh) {
    bool over = speedKmh > effectiveLimit() + VIOLATION_MARGIN_KMH;

    if (over && !g_inViolation) {
        g_inViolation = true;
        g_violationStartMs = millis();
        g_violationFired = false;
    }
    if (g_inViolation && !g_violationFired &&
        millis() - g_violationStartMs >= (uint32_t)VIOLATION_MIN_DURATION_S * 1000) {
        char json[384];
        uint32_t durationSec = (millis() - g_violationStartMs) / 1000;
        if (DataBuilder::buildViolation(deviceId(), g_fusion.getData(), effectiveLimit(),
                                        durationSec, json, sizeof(json)) > 0) {
            g_transport.sendViolation(json);   // phone now, or queued until it connects
            Cellular::postViolation(json);
            LOG_INFO("VIOL", "speed=%.1f limit=%.0f", speedKmh, effectiveLimit());
        }
        g_violationFired = true;
    }
    if (!over && g_inViolation) g_inViolation = false;
}

// ---- crash -----------------------------------------------------------------

static void sendCrashAlerts() {
    char link[96];
    mapsLink(link, sizeof(link));
    char sms[161];
    snprintf(sms, sizeof(sms), "NAZER CRASH ALERT (%s, %.0f km/h, %.1fg). Location: %s",
             Vehicle.modeName(), g_crashInfo.speedBeforeKmh, g_crashInfo.peakG, link);
    for (uint8_t i = 0; i < Vehicle.contactCount(); i++) Cellular::sendSms(Vehicle.contact(i), sms);
    if (Vehicle.contactCount() == 0) LOG_WARN("CRASH", "No emergency contacts set: no SMS sent");
}

static void buildCrashEvent(const char* kind, char* out, size_t len) {
    JsonDocument doc;
    doc["event"] = kind;
    doc["event_id"] = g_crashEventId;
    if (strcmp(kind, "crash") == 0) {
        doc["device_id"] = deviceId();
        doc["timestamp"] = DataBuilder::makeTimestamp();
        doc["lat"] = serialized(String(g_lastLat, 6));
        doc["lon"] = serialized(String(g_lastLon, 6));
        doc["speed_kmh"] = serialized(String(g_crashInfo.speedBeforeKmh, 1));
        doc["peak_g"] = serialized(String(g_crashInfo.peakG, 1));
        doc["tilt_deg"] = serialized(String(g_crashInfo.tiltDeg, 0));
    }
    serializeJson(doc, out, len);
}

static void handleCrash(float speedKmh) {
    IMUData imu = g_fusion.imu().getData();
    if (imu.dataReady && !g_crashPending &&
        g_crash.update(imu.rawX, imu.rawY, imu.rawZ, speedKmh, Vehicle.crashThresholds())) {
        g_crashPending = true;
        g_crashAtMs = millis();
        g_crashInfo = g_crash.last();
        uint32_t t = unixNow();
        snprintf(g_crashEventId, sizeof(g_crashEventId), "%s_%lu", deviceId().c_str(),
                 (unsigned long)(t ? t : millis() / 1000));
        LOG_WARN("CRASH", "DETECTED (%s) peak=%.1fg tilt=%.0f speed_before=%.0f, %lus to cancel",
                 g_crashInfo.reason, g_crashInfo.peakG, g_crashInfo.tiltDeg,
                 g_crashInfo.speedBeforeKmh, (unsigned long)(CRASH_GRACE_MS / 1000));
        char ev[320];
        buildCrashEvent("crash", ev, sizeof(ev));
        g_transport.ble().notifyJson(ev);
    }

    if (g_crashPending && millis() - g_crashAtMs >= CRASH_GRACE_MS) {
        g_crashPending = false;
        LOG_WARN("CRASH", "Not cancelled: alerting contacts");
        char ev[320];
        buildCrashEvent("crash", ev, sizeof(ev));
        Cellular::postEvent(ev);
        sendCrashAlerts();
    }
}

static void cancelCrash(const char* by) {
    if (!g_crashPending) return;
    g_crashPending = false;
    LOG_INFO("CRASH", "Cancelled by %s", by);
    char ev[96];
    buildCrashEvent("crash_cancelled", ev, sizeof(ev));
    g_transport.ble().notifyJson(ev);
}

// ---- commands from the app (BLE) ---------------------------------------------

static void bleAuthReply(const char* status, const char* reason = nullptr) {
    JsonDocument doc;
    doc["auth"] = status;
    if (reason) doc["reason"] = reason;
    char buf[96];
    serializeJson(doc, buf, sizeof(buf));
    g_transport.ble().notifyAuthStatus(buf);
}

static void handleBleCommands() {
    static bool wasConnected = false;
    bool connected = g_transport.ble().clientConnected();
    if (connected && !wasConnected) {
        setParked(false);
        // Nothing (config, telemetry, events) is shared until the phone pairs.
        bleAuthReply("required", g_bleAuth.configured() ? nullptr : "not_configured");
    }
    wasConnected = connected;

    char raw[BLE_CMD_MAX];
    while (g_transport.ble().popCommand(raw, sizeof(raw))) {
        JsonDocument doc;
        if (deserializeJson(doc, raw)) {
            LOG_WARN("BLE", "Bad command JSON");   // not echoed: may hold a pairing code
            continue;
        }
        const char* cmd = doc["cmd"] | "";
        if (strcmp(cmd, "auth") == 0) {
            BleAuth::Result r = g_bleAuth.check(doc["code"] | "", millis());
            if (r == BleAuth::OK) {
                g_transport.ble().setAuthorized(true);
                bleAuthReply("ok");
                LOG_INFO("BLE", "Phone paired");
                notifyConfig();   // the app gets the current setup once paired
            } else {
                bleAuthReply("failed", BleAuth::reason(r));
                LOG_WARN("BLE", "Pairing refused: %s", BleAuth::reason(r));
            }
            continue;
        }
        if (!BleAuth::allowed(cmd, g_transport.ble().authorized())) {
            bleAuthReply("required");
            LOG_WARN("BLE", "Command refused before pairing: %s", cmd);
            continue;
        }
        bool changed = false;
        if (strcmp(cmd, "set_limit") == 0) {
            changed = setLimit(doc["value"] | 0);
        } else if (strcmp(cmd, "set_saver") == 0 && doc["value"].is<bool>()) {
            g_saver=doc["value"].as<bool>(); Storage.setSaver(g_saver); changed=true;
        } else if (strcmp(cmd, "set_mode") == 0) {
            changed = Vehicle.setModeByName(doc["value"] | "");
            if (changed) g_userLimitKmh=Storage.getSpeedLimit(Vehicle.defaultLimitKmh());
        } else if (strcmp(cmd, "set_contacts") == 0) {
            const char* nums[VehicleConfig::MAX_CONTACTS];
            uint8_t n = 0;
            for (JsonVariant v : doc["value"].as<JsonArray>()) {
                if (n < VehicleConfig::MAX_CONTACTS && v.is<const char*>()) nums[n++] = v.as<const char*>();
            }
            Vehicle.setContacts(nums, n);
            changed = true;
        } else if (strcmp(cmd, "cancel_crash") == 0) {
            cancelCrash("app");
        } else if (strcmp(cmd, "claim") == 0) {
            // Answer the server's one-time claim challenge (ClaimResponse.h).
            // Only reachable after pairing, over the bonded, encrypted link.
            const char* nonce = doc["nonce"] | "";
            char answer[65];
            JsonDocument reply;
            JsonObject r = reply["claim"].to<JsonObject>();
            r["device_id"] = deviceId();
            r["nonce"] = nonce;
            if (claim::response(DEVICE_SECRET_KEY, nonce, deviceId().c_str(), answer, sizeof(answer))) {
                r["response"] = answer;
            } else {
                r["error"] = "bad_nonce";
            }
            char buf[256];
            serializeJson(reply, buf, sizeof(buf));
            g_transport.ble().notifyJson(buf);
        } else if (strcmp(cmd, "get_config") == 0) {
            notifyConfig();
        } else if (strcmp(cmd, "calibrate_imu") == 0) {
            LOG_INFO("MAIN", "IMU calibration: keep the device still and level");
            g_fusion.calibrateImu();
            changed = true;   // config reply carries imu_calibrated so the app sees the result
        } else {
            LOG_WARN("BLE", "Unknown command: %s", cmd);
        }
        if (changed) notifyConfig();
    }
}

// ---- commands by SMS (contacts only) -------------------------------------------
// LOC | STATUS | SAVER ON/OFF | OK (cancel crash). LIMIT and MODE only reply with a
// pointer to Bluetooth: SMS senders can be spoofed, so nothing that sets the limit runs here.

static void handleSmsCommands() {
    InboundSms in;
    while (Cellular::popInboundSms(in)) {
        if (!Vehicle.isContact(in.from)) {
            LOG_INFO("SMS", "Ignored message from non-contact %s", in.from);
            continue;
        }
        char from[12];
        VehicleConfig::normalizeNumber(in.from, from);
        String text = String(in.text);
        text.trim();
        text.toUpperCase();

        char reply[161];
        char link[96];
        mapsLink(link, sizeof(link));
        if (text == "LOC" || text == "WHERE") {
            snprintf(reply, sizeof(reply), "NAZER location: %s", link);
        } else if (text == "STATUS") {
            CellularStatus cs = Cellular::status();
            snprintf(reply, sizeof(reply), "NAZER %s %.0f km/h, limit %.0f, sats %u, signal %u. %s",
                     Vehicle.modeName(), g_fusion.getFusedSpeedKmh(), effectiveLimit(),
                     g_fusion.getData().satellites, cs.csq, link);
        } else if (text == "SAVER ON" || text == "SAVER OFF") {
            g_saver=text=="SAVER ON"; Storage.setSaver(g_saver);
            snprintf(reply,sizeof(reply),"NAZER saver %s",g_saver ? "ON" : "OFF");
            notifyConfig();
        } else if (text.startsWith("LIMIT ")) {
            snprintf(reply,sizeof(reply),"NAZER change the user limit over Bluetooth");
        } else if (text.startsWith("MODE ")) {
            // Mode sets the default speed limit, so it needs a paired phone, not an SMS.
            snprintf(reply, sizeof(reply), "NAZER change the vehicle mode over Bluetooth");
        } else if (text == "OK") {
            bool was = g_crashPending;
            cancelCrash("SMS");
            snprintf(reply, sizeof(reply), was ? "NAZER crash alert cancelled" : "NAZER no crash alert pending");
        } else {
            snprintf(reply, sizeof(reply), "NAZER commands: LOC, STATUS, SAVER ON/OFF, OK");
        }
        Cellular::sendSms(from, reply);
    }
}


static void stateEvent(const char* kind, int battery) {
    JsonDocument doc;
    doc["event"]=kind;
    doc["event_id"]=deviceId()+"_"+String(unixNow())+"_"+kind+"_"+String(millis());
    doc["device_id"]=deviceId(); doc["timestamp"]=DataBuilder::makeTimestamp();
    doc["lat"]=serialized(String(g_lastLat,6)); doc["lon"]=serialized(String(g_lastLon,6));
    if (battery>=0) doc["battery_pct"]=battery;
    char out[384]; serializeJson(doc,out,sizeof(out));
    g_transport.ble().notifyJson(out);
    Cellular::postEvent(out);
}
static void setParked(bool value) {
    if (value==g_parked) return;
    g_parked=value;
    g_stillTimer.reset();
    g_lastCloudMs=millis();
    Cellular::setParked(value);
    setCpuFrequencyMhz(value ? 80 : 240);
    g_transport.ble().setParked(value);
    stateEvent(value ? "parked" : "moving");
}
static void checkBattery(int percent) {
    if (g_batteryAlert.update(percent)) stateEvent("low_battery",percent);
}
static void updateState(uint32_t now) {
    auto gps=g_fusion.gps().getData();
    auto imu=g_fusion.imu().getData();
    // Use calibrated gravity-free acceleration; missing/uncalibrated IMU
    // cannot prove stillness and therefore cannot enter parked mode.
    constexpr float IMU_MOVING_THRESHOLD_MS2=0.3f; // ten times measured rest noise (~0.03 m/s²)
    bool imuStill=imu.dataReady && imu.calibrated &&
        sqrtf(imu.accelX*imu.accelX+imu.accelY*imu.accelY+imu.accelZ*imu.accelZ)<IMU_MOVING_THRESHOLD_MS2;
    bool motion=imu.dataReady && imu.calibrated && !imuStill;
    if (g_parked && (motion || (gps.hasFix && gps.rawSpeedKmh>8) || g_transport.ble().clientConnected())) setParked(false);
    if (!g_parked && g_stillTimer.update(now,gps.hasFix && gps.rawSpeedKmh<3 && imuStill && !g_transport.ble().clientConnected())) setParked(true);
    if (gps.hasFix) {
        if (!g_hadFix || g_gpsLost) stateEvent("gps_ok");
        g_hadFix=true; g_gpsLost=false;
    } else if (g_hadFix && !g_gpsLost && now-g_lastFixMs>=60000) {
        g_gpsLost=true; stateEvent("gps_lost");
    }
    if (!g_lastRoadMs || now-g_lastRoadMs>=1000) {
        g_lastRoadMs=now;
        int road=gps.hasFix ? g_roads.lookup(gps.lat,gps.lon,gps.headingDeg,gps.rawSpeedKmh) : 0;
        auto cs=Cellular::status();
        if (!road && cs.lastPostOkMs && now-cs.lastPostOkMs<120000) road=cs.serverLimitKmh;
        g_roadLimitKmh=road;
    }
}
