// =============================================================
// DataBuilder.cpp: Implementation
// =============================================================

#include "DataBuilder.h"
#include "config.h"   // NAZER_FW_VERSION
#include <time.h>

// ============================================================
// makeTimestamp()
// Returns an ISO8601 UTC timestamp.
// The clock is set from GPS date/time by main.cpp.
// ============================================================
String DataBuilder::makeTimestamp() {
    // Real UTC once main.cpp has set the clock from GPS; before the first
    // fix, fall back to uptime on 1970-01-01 (the backend then uses
    // received_at for ordering).
    time_t now = time(nullptr);
    char buf[32];
    if (now > 1700000000) {
        struct tm t;
        gmtime_r(&now, &t);
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
        return String(buf);
    }
    uint32_t ms  = millis();
    uint32_t sec = ms / 1000;
    snprintf(buf, sizeof(buf), "1970-01-01T%02lu:%02lu:%02lu.%03luZ",
             (unsigned long)((sec / 3600) % 24), (unsigned long)((sec / 60) % 60),
             (unsigned long)(sec % 60), (unsigned long)(ms % 1000));
    return String(buf);
}

// ============================================================
// buildTelemetry()
// Matches POST /v1/device/telemetry body from backend spec.
//
// Field mapping:
//   device_id      ← deviceId (MAC-derived, from Storage)
//   timestamp      ← makeTimestamp() (pseudo-UTC for now)
//   lat            ← gps.lat
//   lon            ← gps.lon
//   speed_kmh      ← fused.fusedSpeedKmh    (primary fused speed)
//   speed_gps_kmh  ← gps.rawSpeedKmh        (raw GPS before Kalman)
//   speed_imu_kmh  ← not sent: no IMU-only speed estimate exists yet
//   heading_deg    ← gps.headingDeg
//   speed_limit_kmh← caller provides (from Storage or last server response)
//   altitude_m     ← gps.altitudeM
//   satellites     ← gps.satellites
//   hdop           ← gps.hdop
//   battery_pct    ← caller provides (ADC not in Phase 4.5)
//   gsm_signal     ← caller provides (0 in Phase 4.5)
//   is_stationary  ← gps.isStationary
//   accel_x/y/z    ← imu.accelX/Y/Z (calibrated, gravity removed)
// ============================================================
size_t DataBuilder::buildTelemetry(
    const String&    deviceId,
    const FusedData& fused,
    const GPSData&   gps,
    const IMUData&   imu,
    int     speedLimitKmh,
    uint8_t battPct,
    uint8_t gsmSignal,
    char*   out,
    size_t  outLen,
    const char* vehicleMode,
    bool    overLimit)
{
    // JsonDocument sizing:
    // A telemetry packet has ~17 fields. ArduinoJson 7 dynamic doc
    // automatically manages its own memory: no size constant needed.
    JsonDocument doc;

    doc["device_id"]       = deviceId;
    doc["timestamp"]       = makeTimestamp();

    // Position: 6 decimal places is ~0.1m precision (more than GPS accuracy)
    doc["lat"]             = serialized(String(gps.lat,  6));
    doc["lon"]             = serialized(String(gps.lon,  6));

    // Speed: 1 decimal place is sufficient (GPS noise > 0.1 km/h)
    doc["speed_kmh"]       = serialized(String(fused.fusedSpeedKmh,  1));
    doc["speed_gps_kmh"]   = serialized(String(gps.rawSpeedKmh,      1));
    // speed_imu_kmh is left out on purpose. The firmware has no IMU-only
    // speed estimate, and sending the fused value under this key made the
    // two columns identical. The backend treats the field as optional.

    doc["heading_deg"]     = serialized(String(gps.headingDeg,       1));
    doc["speed_limit_kmh"] = speedLimitKmh;
    doc["altitude_m"]      = serialized(String(gps.altitudeM,        1));
    doc["satellites"]      = gps.satellites;
    doc["hdop"]            = serialized(String(gps.hdop,             2));
    doc["battery_pct"]     = battPct;
    doc["gsm_signal"]      = gsmSignal;
    doc["is_stationary"]   = gps.isStationary;

    // IMU: calibrated acceleration (gravity removed, should be ~0 at rest)
    doc["accel_x"]         = serialized(String(imu.accelX, 4));
    doc["accel_y"]         = serialized(String(imu.accelY, 4));
    doc["accel_z"]         = serialized(String(imu.accelZ, 4));

    // v2 (contracts.md, 2026-10-16): optional for older apps/backends
    doc["gps_fix"]         = fused.hasFix;
    doc["over_limit"]      = overLimit;
    doc["fw"]              = NAZER_FW_VERSION;
    if (vehicleMode) doc["vehicle_mode"] = vehicleMode;

    size_t written = serializeJson(doc, out, outLen);

    if (written == 0 || written >= outLen) {
        Serial.printf("[E][DATABUILDER] Telemetry JSON overflow! buf=%u written=%u\n",
                      (unsigned)outLen, (unsigned)written);
        return 0;
    }
    return written;
}

// ============================================================
// buildViolation()
// Matches POST /v1/device/violation body from backend spec.
//
// Note: location_name (reverse-geocoded address) is NOT included
// here: it is resolved server-side from lat/lon. The device
// sends coordinates only.
// ============================================================
size_t DataBuilder::buildViolation(
    const String&    deviceId,
    const FusedData& fused,
    float   speedLimitKmh,
    uint32_t durationSec,
    char*   out,
    size_t  outLen)
{
    JsonDocument doc;

    doc["device_id"]       = deviceId;
    doc["timestamp"]       = makeTimestamp();
    doc["lat"]             = serialized(String(fused.lat, 6));
    doc["lon"]             = serialized(String(fused.lon, 6));
    doc["speed_kmh"]       = serialized(String(fused.fusedSpeedKmh, 1));
    doc["speed_limit_kmh"] = serialized(String(speedLimitKmh,       1));
    doc["duration_sec"]    = durationSec;

    size_t written = serializeJson(doc, out, outLen);

    if (written == 0 || written >= outLen) {
        Serial.printf("[E][DATABUILDER] Violation JSON overflow! buf=%u written=%u\n",
                      (unsigned)outLen, (unsigned)written);
        return 0;
    }
    return written;
}