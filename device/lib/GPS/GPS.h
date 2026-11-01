#pragma once
// ============================================================
// GPS.h: Ai-Thinker GP-02 driver for NAZER
//
// Hardware:
//   GP-02 dev board: BDS+GPS dual mode, NMEA output at 9600 baud
//   Connected to UART1: RX=GPIO4 (← GP-02 TX), TX=GPIO5 (→ GP-02 RX)
//
// What this module does:
//   - Reads NMEA sentences from UART1 using TinyGPS++
//   - Extracts speed, position, heading, satellites, HDOP
//   - Applies stationary detection with hysteresis so the device
//     doesn't show 0.3–1.5 km/h noise when parked
//   - Tracks time-to-first-fix for diagnostics
//   - Outputs a clean GPSData struct every update cycle
//
// Usage:
//   GPS gps;
//   gps.begin();           // call once in setup()
//   gps.update();          // call every loop() iteration
//   GPSData d = gps.getData();
//   if (d.hasFix) { use d.speedKmh, d.lat, d.lon ... }
// ============================================================

#include <Arduino.h>
#include <TinyGPS++.h>

// ---- Data structure returned to callers ----
struct GPSData {
    // Position
    double  lat;            // decimal degrees, positive = North
    double  lon;            // decimal degrees, positive = East
    float   altitudeM;      // metres above sea level
    float   headingDeg;     // course over ground, 0–360°

    // Speed
    float   speedKmh;       // fused/filtered speed (after stationary detection)
    float   rawSpeedKmh;    // direct from GPS, before stationary filter

    // Fix quality
    bool    hasFix;         // true when satellites >= 3 and HDOP < 5.0
    uint8_t satellites;     // number of satellites in use
    float   hdop;           // horizontal dilution of precision (lower = better)

    // Stationary state
    bool    isStationary;   // true when speed is below threshold

    // Timing
    uint32_t lastFixMs;     // millis() when fix was last confirmed
    uint32_t ttffMs;        // time-to-first-fix in ms (0 = not yet acquired)

    // Raw NMEA sentence count: useful to verify data is flowing
    uint32_t sentencesProcessed;
};

// ============================================================
class GPS {
public:
    GPS();

    // Call once in setup(). Starts UART1 on GPS_RX_PIN / GPS_TX_PIN.
    void begin();

    // Call every loop() iteration. Feeds available UART bytes into
    // TinyGPS++ and updates internal state. Returns true if a new
    // complete NMEA sentence was parsed this call.
    bool update();

    // Returns a copy of the latest GPS data.
    GPSData getData() const { return _data; }

    // UTC unix time from the last RMC/ZDA sentence; false until the GPS has date+time.
    bool utcEpoch(uint32_t& out);

    // True if at least one valid fix has been received since boot.
    bool hasFix() const { return _data.hasFix; }

    // Current speed after stationary filter (km/h).
    float speedKmh() const { return _data.speedKmh; }

    // Convenience: is the device not moving?
    bool isStationary() const { return _data.isStationary; }

    // Print a one-line CSV header to Serial (for spreadsheet logging)
    static void printCsvHeader();

    // Print one CSV row for the current data (call after update())
    void printCsvRow() const;

private:
    TinyGPSPlus  _gps;          // TinyGPS++ parser instance
    HardwareSerial _serial;     // UART1: GPS_RX_PIN / GPS_TX_PIN
    GPSData      _data;         // latest parsed data

    uint32_t     _bootMs;       // millis() at begin(): for TTFF calculation
    bool         _ttffRecorded; // have we recorded the first fix time?

    // ---- Stationary detection state ----
    // Hysteresis prevents bouncing between moving/stopped at the threshold boundary.
    // To transition moving→stopped:  speed must drop below STATIONARY_THRESHOLD_KMH
    // To transition stopped→moving:  speed must exceed MOVING_THRESHOLD_KMH
    // Both thresholds defined in config.h.
    void _updateStationaryState(float rawSpeed, float hdop);
    uint8_t _aboveCount = 0;     // consecutive fixes above the moving threshold
    uint8_t _belowCount = 0;     // consecutive fixes below the stationary threshold
    bool    _anchored   = false; // position held while stationary
    double  _anchorLat  = 0.0;
    double  _anchorLon  = 0.0;
    float   _anchorHeading = 0.0f;

    // ---- Internal helpers ----
    void _applyFix();           // copy TinyGPS++ values into _data
};