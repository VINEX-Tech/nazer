// ============================================================
// GPS.cpp: Implementation
// ============================================================

#include "GPS.h"
#include "../../include/config.h"
#include "../Logger/Logger.h"

GPS::GPS()
    : _serial(1)   // UART1: leaves UART0 free for Serial Monitor
    , _bootMs(0)
    , _ttffRecorded(false)
{
    memset(&_data, 0, sizeof(_data));
    _data.hdop = 99.9f;  // initialise to "worst possible" so UI shows no fix
}

void GPS::begin() {
    _bootMs = millis();

    // Start UART1 on the correct pins (defined in config.h).
    // GPS_TX_PIN is the ESP32 TX → GP-02 RX. We rarely need to send
    // commands to the GPS module, but the pin must be specified.
    _serial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    LOG_INFO("GPS", "UART1 started: RX=GPIO%d TX=GPIO%d @ %d baud",
             GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
    LOG_INFO("GPS", "Waiting for NMEA sentences...");
}

bool GPS::update() {
    // Feed all available bytes from UART into TinyGPS++.
    // TinyGPS++ silently accumulates characters until it has a
    // complete sentence, then parses it internally.
    bool newSentence = false;
    while (_serial.available() > 0) {
        char c = _serial.read();
        if (_gps.encode(c)) {
            newSentence = true;  // a complete NMEA sentence was just parsed
        }
    }

    // Even if no new sentence arrived, update sentence count
    _data.sentencesProcessed = _gps.sentencesWithFix()
                              + _gps.failedChecksum()
                              + _gps.passedChecksum();

    // Only update our data struct when TinyGPS++ has fresh location data.
    // isUpdated() is true once per new sentence that contained location.
    if (_gps.location.isUpdated() || _gps.speed.isUpdated()) {
        _applyFix();
    }

    return newSentence;
}

void GPS::_applyFix() {
    // ---- Determine fix validity ----
    // We consider a fix valid when:
    //   - location is valid (GPRMC returned an 'A' status, not 'V')
    //   - at least 3 satellites (minimum for 2D fix)
    //   - HDOP < 5.0 (coarse quality gate: 5.0 is quite loose, tighten later)
    bool locationValid = _gps.location.isValid();
    uint8_t sats       = (uint8_t)_gps.satellites.value();
    float   hdop       = _gps.hdop.isValid() ? (float)_gps.hdop.hdop() : 99.9f;

    _data.hasFix    = locationValid && (sats >= 3) && (hdop < 5.0f);
    _data.satellites = sats;
    _data.hdop       = hdop;

    // ---- Record time-to-first-fix (only once) ----
    if (_data.hasFix && !_ttffRecorded) {
        _data.ttffMs    = millis() - _bootMs;
        _ttffRecorded   = true;
        LOG_INFO("GPS", "First fix acquired! TTFF=%lu ms  Sats=%d  HDOP=%.1f",
                 _data.ttffMs, sats, hdop);
    }

    if (_data.hasFix) {
        _data.lastFixMs = millis();

        // ---- Position ----
        _data.lat        = _gps.location.lat();
        _data.lon        = _gps.location.lng();
        _data.altitudeM  = _gps.altitude.isValid() ? (float)_gps.altitude.meters() : 0.0f;
        _data.headingDeg = _gps.course.isValid()   ? (float)_gps.course.deg()       : 0.0f;

        // ---- Raw speed from GPS ----
        // TinyGPS++ gives speed in knots from GPRMC; convert to km/h.
        float rawSpeed = _gps.speed.isValid()
                       ? (float)_gps.speed.kmph()
                       : 0.0f;
        _data.rawSpeedKmh = rawSpeed;

        // ---- Stationary detection ----
        _updateStationaryState(rawSpeed, hdop);

        // While stopped, GPS position and course wander by metres and degrees.
        // Report the point where the vehicle stopped instead of the noise.
        if (_data.isStationary) {
            if (!_anchored) {
                _anchorLat = _data.lat; _anchorLon = _data.lon;
                _anchorHeading = _data.headingDeg; _anchored = true;
            }
            _data.lat = _anchorLat; _data.lon = _anchorLon;
            _data.headingDeg = _anchorHeading;
        } else {
            _anchored = false;
        }
    }
    // If fix lost, keep last known position but mark as no-fix.
    // Speed goes to zero since we can't trust stale velocity.
    else {
        _data.speedKmh    = 0.0f;
        _data.rawSpeedKmh = 0.0f;
        _data.isStationary = true;
        _anchored = false;   // may have moved while blind; re-anchor on the next fix
        _aboveCount = 0;
    }
}

void GPS::_updateStationaryState(float rawSpeed, float hdop) {
    // A parked receiver reports 1-5 km/h of noise (worse with a weak sky view), so
    // a single reading above a low threshold is not motion. Change state only after
    // GPS_STATE_CONFIRM consecutive fixes agree, and demand more speed when HDOP is poor.
    float movingKmh = hdop > GPS_POOR_HDOP ? MOVING_THRESHOLD_POOR_KMH : MOVING_THRESHOLD_KMH;

    if (_data.isStationary) {
        _aboveCount = rawSpeed > movingKmh ? _aboveCount + 1 : 0;
        if (_aboveCount >= GPS_STATE_CONFIRM) {
            _data.isStationary = false;
            _aboveCount = 0;
            LOG_DEBUG("GPS", "State: STATIONARY → MOVING  speed=%.1f km/h hdop=%.1f", rawSpeed, hdop);
        }
    } else {
        _belowCount = rawSpeed < STATIONARY_THRESHOLD_KMH ? _belowCount + 1 : 0;
        if (_belowCount >= GPS_STATE_CONFIRM) {
            _data.isStationary = true;
            _belowCount = 0;
            LOG_DEBUG("GPS", "State: MOVING → STATIONARY  speed=%.1f km/h", rawSpeed);
        }
    }
    _data.speedKmh = _data.isStationary ? 0.0f : rawSpeed;
}

// ============================================================
// CSV logging helpers (for Kalman tuning data collection)
// ============================================================

void GPS::printCsvHeader() {
    Serial.println(F("ms,hasFix,lat,lon,speed_kmh,raw_speed_kmh,heading_deg,"
                     "alt_m,sats,hdop,isStationary,sentences"));
}

void GPS::printCsvRow() const {
    // Format matches the header above: paste output into a spreadsheet
    // after a calibration drive to tune Kalman R_gps.
    Serial.printf("%lu,%d,%.6f,%.6f,%.2f,%.2f,%.1f,%.1f,%d,%.2f,%d,%lu\n",
                  millis(),
                  (int)_data.hasFix,
                  _data.lat,
                  _data.lon,
                  _data.speedKmh,
                  _data.rawSpeedKmh,
                  _data.headingDeg,
                  _data.altitudeM,
                  _data.satellites,
                  _data.hdop,
                  (int)_data.isStationary,
                  _data.sentencesProcessed);
}
// -----------------------------------------------------------------------
// utcEpoch: civil date to unix seconds (no timegm on newlib; days_from_civil)
// -----------------------------------------------------------------------
bool GPS::utcEpoch(uint32_t& out) {
    if (!_gps.date.isValid() || !_gps.time.isValid() || _gps.date.year() < 2024) return false;
    if (_gps.date.age() > 5000) return false;
    int y = _gps.date.year(), m = _gps.date.month(), d = _gps.date.day();
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = (long)era * 146097 + doe - 719468;
    out = (uint32_t)(days * 86400L + _gps.time.hour() * 3600L + _gps.time.minute() * 60L + _gps.time.second());
    return true;
}
