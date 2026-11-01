// ============================================================
// test_fusion.cpp: Phase 4: Sensor Fusion Test
//
// Tests GPS + IMU + Kalman filter running together.
//
// What this sketch does:
//   1. Boots with GPS + IMU + Kalman filter active
//   2. Shows live fused speed on OLED MainScreen
//   3. Streams CSV to Serial for Kalman tuning analysis
//   4. Pressing the encoder button triggers IMU re-calibration
//   5. After calibration, tracks speed normally
//
// OLED layout during test:
//   Line 1: "FUSION TEST"
//   Line 2: "GPS: XX.X km/h"
//   Line 3: "IMU: ±X.XXX m/s²"
//   Line 4: "FUS: XX.X km/h  K:0.xx"
//   Line 5: "Sats:N HDOP:X.X"
//
// Serial CSV format (paste into spreadsheet after drive):
//   ms,hasFix,sats,hdop,gps_kmh,imu_accel_ms2,fused_kmh,
//   kalman_gain,covariance,isStationary,gps_updated,lat,lon
//
// platformio.ini:
//   build_src_filter = -<*> +<../test/test_fusion.cpp>
//
// Pass criteria (check against NAZER_SESSION_SUMMARY.md):
//   [x] fused_kmh = 0.00 when stationary (no IMU drift)
//   [x] fused_kmh tracks gps_kmh within ±2 km/h during steady driving
//   [x] fused_kmh responds faster than gps_kmh to acceleration/braking
//   [x] No oscillation or instability in fused output
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include "../../include/config.h"
#include "../Logger/Logger.h"
#include "../Display/Display.h"
#include "../Display/Screens/MainScreen.h"
#include "../Encoder/Encoder.h"
#include "../Fusion/SensorFusion.h"

// ---- Module instances ----
static Encoder      encoder(ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN);
static SensorFusion fusion;

// ---- State ----
static bool     calibrationRequested = false;
static bool     csvHeaderPrinted     = false;
static uint32_t lastDisplayMs        = 0;
static uint32_t lastCsvMs            = 0;
static uint32_t lastStatusMs         = 0;

// CSV print interval in ms: 200ms = 5 Hz (enough data density for tuning)
static constexpr uint32_t CSV_INTERVAL_MS    = 200;
// Status heartbeat interval
static constexpr uint32_t STATUS_INTERVAL_MS = 5000;

// ============================================================
// OLED render: updates at DISPLAY_REFRESH_MS (100ms = 10 Hz)
// ============================================================
static void renderDisplay(const FusedData& d) {
    Display.clear();

    // Line 0: Header + fix indicator
    if (d.hasFix) {
        Display.drawString(0, 0, "FUSION  FIX OK");
    } else {
        Display.drawString(0, 0, "FUSION  NO FIX");
    }

    // Line 1: GPS raw speed
    char buf[32];
    snprintf(buf, sizeof(buf), "GPS: %5.1f km/h", d.gpsSpeedKmh);
    Display.drawString(0, 12, buf);

    // Line 2: IMU longitudinal acceleration
    snprintf(buf, sizeof(buf), "IMU:%+6.3f m/s2", d.imuAccelMs2);
    Display.drawString(0, 24, buf);

    // Line 3: Fused speed (primary output) + Kalman gain
    snprintf(buf, sizeof(buf), "FUS: %5.1f  K%.2f", d.fusedSpeedKmh, d.kalmanGain);
    Display.drawString(0, 36, buf);

    // Line 4: Satellite count and HDOP
    snprintf(buf, sizeof(buf), "Sat:%d HDOP:%.1f", d.satellites, d.hdop);
    Display.drawString(0, 52, buf);

    Display.flush();
}

// ============================================================
// OLED calibration screen: shown during the 3-second blocking calibrate()
// ============================================================
static void renderCalibScreen(uint8_t pct) {
    Display.clear();
    Display.drawString(0, 0,  "IMU CALIBRATION");
    Display.drawString(0, 14, "Keep flat+still!");

    // Progress bar: 100px wide, 8px tall
    uint8_t barW = (uint8_t)((pct * 100) / 100);
    Display.drawRect(14, 32, 100, 8, false);        // border
    Display.drawRect(14, 32, barW, 8, true);        // fill

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    Display.drawString(55, 44, buf);
    Display.flush();
}

// ============================================================
// setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println(F("\n\n=================================="));
    Serial.println(F("  NAZER Phase 4: Sensor Fusion"));
    Serial.println(F("=================================="));

    // ---- Display ----
    Wire.begin(SDA_PIN, SCL_PIN);
    Display.begin();
    Display.clear();
    Display.drawString(0, 0, "NAZER Phase 4");
    Display.drawString(0, 14, "Sensor Fusion");
    Display.drawString(0, 28, "Initialising...");
    Display.flush();

    // ---- Encoder ----
    encoder.begin();
    Serial.println(F("[I][MAIN] Encoder ready"));

    // ---- Sensor Fusion (GPS + IMU + Kalman) ----
    bool fusionOk = fusion.begin();

    if (!fusionOk) {
        Display.clear();
        Display.drawString(0, 0, "IMU INIT FAILED");
        Display.drawString(0, 14, "Check I2C wiring");
        Display.drawString(0, 28, "SDA=21 SCL=22");
        Display.flush();
        Serial.println(F("[E][MAIN] Halting: fix IMU wiring"));
        while (true) { delay(1000); }
    }

    fusion.imu().setCalibrationProgressCallback([](uint8_t pct) {
        renderCalibScreen(pct);
    });

    if (!fusion.imuCalibrated()) {
        Display.clear();
        Display.drawString(0, 0,  "IMU not calibrated");
        Display.drawString(0, 14, "Place flat+still");
        Display.drawString(0, 28, "Press btn to cal");
        Display.flush();
        Serial.println(F("[W][MAIN] IMU not calibrated: press encoder button to calibrate now"));
    } else {
        Display.clear();
        Display.drawString(0, 0, "Waiting for GPS...");
        Display.flush();
        Serial.println(F("[I][MAIN] IMU calibration loaded. Waiting for GPS fix..."));
    }

    Serial.println(F("[I][MAIN] Setup complete. Serial CSV will start after GPS fix.\n"));
    Serial.println(F("Press encoder button to re-calibrate IMU at any time.\n"));
}

// ============================================================
// loop()
// ============================================================
void loop() {
    uint32_t now = millis();

    // ---- Poll encoder button ----
    encoder.getStep(); // Drain the rotary buffer so it doesn't overflow
    if (encoder.wasPressed()) {
        calibrationRequested = true;
        Serial.println(F("[I][MAIN] Calibration requested by user"));
    }

    // ---- Calibration (blocking) ----
    if (calibrationRequested) {
        calibrationRequested = false;
        fusion.calibrateImu();
        fusion.gps();    // Keep GPS parser alive
        Serial.println(F("[I][MAIN] Calibration done: Kalman filter resetting"));
    }

    // ---- Main fusion update ----
    fusion.update();
    FusedData d = fusion.getData();

    // ---- Print CSV header once after first GPS fix ----
    if (!csvHeaderPrinted && d.hasFix) {
        SensorFusion::printCsvHeader();
        csvHeaderPrinted = true;
        Serial.println(F("# CSV started: copy rows to spreadsheet for Kalman tuning"));
    }

    // ---- Stream CSV at 5 Hz ----
    if (csvHeaderPrinted && (now - lastCsvMs >= CSV_INTERVAL_MS)) {
        fusion.printCsvRow();
        lastCsvMs = now;
    }

    // ---- Update OLED at 10 Hz ----
    if (now - lastDisplayMs >= DISPLAY_REFRESH_MS) {
        renderDisplay(d);
        lastDisplayMs = now;
    }

    // ---- Periodic status log to Serial (every 5 seconds) ----
    if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
        lastStatusMs = now;

        if (!d.hasFix) {
            Serial.printf("[I][MAIN] Waiting for GPS fix... sentences=%lu\n",
                          fusion.gps().getData().sentencesProcessed);
        } else {
            Serial.printf("[I][MAIN] Running | GPS=%.1f  IMU=%.3f  FUSED=%.1f | "
                          "Sats=%d HDOP=%.1f | K=%.3f P=%.4f\n",
                          d.gpsSpeedKmh, d.imuAccelMs2, d.fusedSpeedKmh,
                          d.satellites, d.hdop,
                          d.kalmanGain, d.kalmanCovariance);
        }
    }
}