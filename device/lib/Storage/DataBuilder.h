#pragma once
// =============================================================
// DataBuilder.h: Constructs API-spec JSON from sensor data
//
// Converts the internal sensor structs (FusedData, GPSData,
// IMUData) into the exact JSON bodies required by the backend
// API spec in NAZER_BACKEND_AND_DATABASE.md.
//
// Why a separate class?
//   - Keeps JSON construction in one place: if the API spec
//     changes, only DataBuilder changes.
//   - Transport classes stay ignorant of field names and units.
//   - Unit-testable without hardware.
//
// Timestamp:
//   The ESP32 has no RTC. For Phase 4.5 (WiFi test), we use
//   millis()-based elapsed time rendered as a pseudo-ISO8601
//   string ("1970-01-01T00:HH:MM:SS.mmmZ"). This is enough for
//   the test server to log sequences.
//   In Phase 5 (GSM), we will sync time via AT+CCLK and produce
//   real UTC timestamps. The JSON field name ("timestamp") and
//   position are identical: no API change required.
//
// Usage:
//   char buf[512];
//   DataBuilder::buildTelemetry(fusedData, buf, sizeof(buf));
//   wifi.sendTelemetry(buf);
//
//   char vbuf[256];
//   DataBuilder::buildViolation(fusedData, 80.0f, 12, vbuf, sizeof(vbuf));
//   wifi.sendViolation(vbuf);
// =============================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include "SensorFusion.h"   // FusedData: PlatformIO adds lib/Fusion/ to include path
#include "GPS.h"            // GPSData
#include "IMU.h"            // IMUData

class DataBuilder {
public:
    // ---- Telemetry packet ----
    // Fills 'out' with JSON matching POST /v1/device/telemetry spec.
    // Requires FusedData (primary), GPSData (raw GPS fields), IMUData
    // (accel x/y/z), plus context values from StorageManager.
    //
    // speedLimitKmh: current speed limit: pass Storage.getSpeedLimit()
    // battPct:       battery percentage (0 in Phase 4.5: ADC not wired yet)
    // gsmSignal:     GSM signal quality (0 in Phase 4.5: GSM not active)
    //
    // Returns number of bytes written (0 on failure).
    static size_t buildTelemetry(
        const String&   deviceId,
        const FusedData& fused,
        const GPSData&   gps,
        const IMUData&   imu,
        int    speedLimitKmh,
        uint8_t battPct,
        uint8_t gsmSignal,
        char*   out,
        size_t  outLen,
        const char* vehicleMode = nullptr,   // v2 optional fields
        bool    overLimit = false
    );

    // ---- Violation packet ----
    // Fills 'out' with JSON matching POST /v1/device/violation spec.
    // durationSec: how many seconds the device has been over the limit.
    //
    // Returns number of bytes written (0 on failure).
    static size_t buildViolation(
        const String&   deviceId,
        const FusedData& fused,
        float  speedLimitKmh,
        uint32_t durationSec,
        char*   out,
        size_t  outLen
    );

    // ---- Helpers ----
    // Returns a pseudo-ISO8601 timestamp string from millis().
    // Format: "1970-01-01T00:HH:MM:SS.mmmZ"
    // Good enough for server log sequencing in Phase 4.5.
    // Will be replaced with real NTP time in Phase 5.
    static String makeTimestamp();

private:
    DataBuilder() = delete; // static-only class
};