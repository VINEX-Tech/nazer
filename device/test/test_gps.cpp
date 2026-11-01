// ============================================================
// test_gps.cpp: NAZER Phase 2 Test
//
// PURPOSE:
//   Verify the GP-02 GPS module is wired correctly, receives NMEA
//   sentences, acquires a fix, and reports accurate speed data.
//
// HOW TO USE:
//   1. Move GPS wire: GP-02 TX → GPIO4 (was GPIO3), GP-02 RX → GPIO5 (was GPIO1)
//   2. In platformio.ini set:
//        build_src_filter = -<*> +<../test/test_gps.cpp>
//   3. Flash, open Serial Monitor at 115200 baud.
//   4. Take device outdoors or near a window with sky view.
//
// PASS CRITERIA (check each manually):
//   [ ] Serial shows "NMEA OK" within 5 seconds: data is flowing from GPS
//   [ ] Fix acquired within 90 seconds outdoors (< 60s with clear sky)
//   [ ] Satellites >= 4 for a valid fix, >= 8 = excellent
//   [ ] HDOP < 2.0 is good, < 1.5 = excellent
//   [ ] Speed shows 0.00 km/h when sitting still (stationary filter working)
//   [ ] OLED updates every second showing fix status and speed
//   [ ] CSV rows printing to Serial (copy to spreadsheet for Kalman tuning)
//
// TROUBLESHOOTING:
//   "NO DATA" after 10s  → GPS still on GPIO3 (UART0). Rewire to GPIO4.
//   Fix never acquired   → Move near window. Cold-start can take 3–5 min.
//   Speed not 0 at rest  → Increase STATIONARY_THRESHOLD_KMH in config.h
//   HDOP always > 5.0    → Poor sky view. Move outside.
//   Garbled NMEA         → Wrong baud rate. GP-02 default is 9600.
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "../lib/Logger/Logger.h"
#include "../lib/Display/Display.h"
#include "../lib/GPS/GPS.h"

// ---- Module instances ----
GPS gps;

// ---- Timing ----
uint32_t lastDisplayUpdate = 0;
uint32_t lastCsvRow        = 0;
uint32_t lastStatusLog     = 0;
uint32_t testStartMs       = 0;

// ---- State ----
bool nmeaConfirmed = false;    // have we seen at least one NMEA sentence?
uint32_t lastSentenceCount = 0;

// ---- Helpers ----
void updateOled(const GPSData& d);
void printStatusLog(const GPSData& d);

// ============================================================
void setup() {
    Logger::begin(115200);
    LOG_INFO("TEST", "=== Phase 2: GPS Test ===");
    LOG_INFO("TEST", "GPS pins: RX=GPIO%d (← GP-02 TX)  TX=GPIO%d (→ GP-02 RX)",
             GPS_RX_PIN, GPS_TX_PIN);
    LOG_INFO("TEST", "Thresholds: MOVING=%.1f km/h  STATIONARY=%.1f km/h",
             (float)MOVING_THRESHOLD_KMH, (float)STATIONARY_THRESHOLD_KMH);

    pinMode(STATUS_LED_PIN, OUTPUT);

    // ---- Display ----
    g_display.begin();
    g_display.showMessage("Phase 2", "GPS Test");
    delay(1500);

    // ---- GPS ----
    gps.begin();

    // Print CSV header now so the Serial log is ready to capture
    LOG_INFO("TEST", "--- CSV log starts below (copy to spreadsheet) ---");
    GPS::printCsvHeader();

    testStartMs = millis();
    LOG_INFO("TEST", "Waiting for NMEA data... (take device near a window)");
}

// ============================================================
void loop() {
    uint32_t now = millis();

    // ---- 1. Feed GPS parser: must run every loop iteration ----
    bool newSentence = gps.update();
    GPSData d = gps.getData();

    // ---- 2. Confirm NMEA data is flowing ----
    if (!nmeaConfirmed && d.sentencesProcessed > 0) {
        nmeaConfirmed = true;
        LOG_INFO("GPS", "NMEA OK: sentences arriving from GP-02");
    }

    // Warn if no data seen after 10 seconds: likely still on wrong GPIO
    if (!nmeaConfirmed && (now - testStartMs) > 10000) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_ERROR("GPS", "NO DATA after 10s! Check: is GP-02 TX wired to GPIO%d?", GPS_RX_PIN);
            LOG_ERROR("GPS", "If GPS was previously on GPIO3, you need to REWIRE the module.");
            g_display.showError("GPS: NO DATA\nCheck GPIO4 wire");
        }
    }

    // ---- 3. Log CSV row every 1 second ----
    if ((now - lastCsvRow) >= 1000) {
        lastCsvRow = now;
        gps.printCsvRow();
    }

    // ---- 4. Human-readable status log every 5 seconds ----
    if ((now - lastStatusLog) >= 5000) {
        lastStatusLog = now;
        printStatusLog(d);
    }

    // ---- 5. Update OLED every second ----
    if ((now - lastDisplayUpdate) >= 1000) {
        lastDisplayUpdate = now;
        updateOled(d);
    }

    // ---- 6. Heartbeat LED ----
    static uint32_t lastBlink = 0;
    if ((now - lastBlink) >= (d.hasFix ? 1000 : 200)) {
        // Fast blink = searching, slow blink = fixed
        lastBlink = now;
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    }
}

// ============================================================
// OLED display layout for GPS test:
//
//   +------------------+
//   | GPS Phase 2 Test |
//   |------------------|
//   | FIX  Sats:8 H:1.2|
//   | 30.044400 N      |
//   | 31.235700 E      |
//   | 65.2 km/h        |
//   +------------------+
// ============================================================
void updateOled(const GPSData& d) {
    g_display.clear();

    // Header
    g_display.drawString(0, 0, "GPS Phase 2 Test");
    g_display.drawLine(0, 11, 127, 11);

    if (!nmeaConfirmed) {
        g_display.drawString(0, 14, "Waiting for NMEA...");
        g_display.drawString(0, 26, "No data from GPS");
        g_display.flush();
        return;
    }

    // Fix status + satellites + HDOP on one line
    char line1[32];
    if (d.hasFix) {
        snprintf(line1, sizeof(line1), "FIX  Sat:%d H:%.1f", d.satellites, d.hdop);
    } else {
        snprintf(line1, sizeof(line1), "NO FIX Sat:%d H:%.1f", d.satellites, d.hdop);
    }
    g_display.drawString(0, 13, line1);

    if (d.hasFix) {
        // Latitude
        char latBuf[24];
        snprintf(latBuf, sizeof(latBuf), "%.5f %c", fabs(d.lat), d.lat >= 0 ? 'N' : 'S');
        g_display.drawString(0, 24, latBuf);

        // Longitude
        char lonBuf[24];
        snprintf(lonBuf, sizeof(lonBuf), "%.5f %c", fabs(d.lon), d.lon >= 0 ? 'E' : 'W');
        g_display.drawString(0, 34, lonBuf);

        // Speed (large-ish)
        char spdBuf[20];
        snprintf(spdBuf, sizeof(spdBuf), "%.1f km/h", d.speedKmh);
        g_display.drawString(0, 44, spdBuf);

        // Stationary indicator
        if (d.isStationary) {
            g_display.drawString(80, 44, "[STA]");
        }
    } else {
        // Show elapsed time while waiting for fix
        uint32_t elapsed = millis() / 1000;
        char waitBuf[24];
        snprintf(waitBuf, sizeof(waitBuf), "Searching... %lus", elapsed);
        g_display.drawString(0, 30, waitBuf);
        g_display.drawString(0, 44, "Move near window");
    }

    g_display.flush();
}

// ============================================================
// Human-readable status log (every 5 seconds)
// ============================================================
void printStatusLog(const GPSData& d) {
    LOG_INFO("GPS", "--- Status @ %lu ms ---", millis());
    LOG_INFO("GPS", "  NMEA flowing:  %s", nmeaConfirmed ? "YES" : "NO");
    LOG_INFO("GPS", "  Has fix:       %s", d.hasFix ? "YES ✓" : "NO");
    LOG_INFO("GPS", "  Satellites:    %d", d.satellites);
    LOG_INFO("GPS", "  HDOP:          %.2f %s",
             d.hdop, d.hdop < 1.5f ? "(excellent)" : d.hdop < 2.5f ? "(good)" : "(poor)");

    if (d.hasFix) {
        LOG_INFO("GPS", "  Lat:           %.6f", d.lat);
        LOG_INFO("GPS", "  Lon:           %.6f", d.lon);
        LOG_INFO("GPS", "  Speed (fused): %.2f km/h", d.speedKmh);
        LOG_INFO("GPS", "  Speed (raw):   %.2f km/h", d.rawSpeedKmh);
        LOG_INFO("GPS", "  Heading:       %.1f deg", d.headingDeg);
        LOG_INFO("GPS", "  Altitude:      %.1f m", d.altitudeM);
        LOG_INFO("GPS", "  Stationary:    %s", d.isStationary ? "YES" : "NO");
    }

    if (d.ttffMs > 0) {
        LOG_INFO("GPS", "  TTFF:          %.1f seconds", d.ttffMs / 1000.0f);
    }

    LOG_INFO("GPS", "  Sentences:     %lu", d.sentencesProcessed);
    LOG_INFO("GPS", "---");
}