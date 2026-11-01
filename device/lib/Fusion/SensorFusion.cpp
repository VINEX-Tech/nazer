// ============================================================
// SensorFusion.cpp: Implementation
// ============================================================

#include "SensorFusion.h"
#include "../Logger/Logger.h"

// ---- Constructor ----
SensorFusion::SensorFusion()
    : _lastImuMs(0)
    , _lastGpsMs(0)
    , _lastCsvMs(0)
    , _lastGpsSentenceCount(0)
{
    memset(&_data, 0, sizeof(_data));
}

// ---- begin() ----
bool SensorFusion::begin() {
    // GPS: non-fatal if no NMEA arrives yet (just need to keep polling)
    _gps.begin();

    // IMU: fatal if chip not found (hardware issue)
    if (!_imu.begin()) {
        LOG_ERROR("FUSION", "IMU init failed: check I2C wiring SDA=21 SCL=22");
        return false;
    }

    // Kalman filter: start at rest
    _kf.begin(0.0f);

    _lastImuMs = millis();
    _lastGpsMs = millis();

    LOG_INFO("FUSION", "SensorFusion ready. Q=%.3f R_gps=%.3f",
             KALMAN_Q, KALMAN_R_GPS);
    return true;
}

// ---- update(): the main loop function ----
// Design notes:
//   - IMU is polled every loop() call (hardware I2C is fast, ~400kHz)
//   - GPS bytes are consumed every loop() call (UART buffer must not overflow)
//   - Kalman predict runs whenever we have a fresh IMU sample
//   - Kalman update runs only when GPS emits a new sentence with speed
void SensorFusion::update() {
    uint32_t now = millis();

    // ---- Always drain the GPS UART buffer ----
    // GPS.update() feeds bytes into TinyGPS++ and returns true when a
    // complete sentence was parsed. We must call this every loop() or
    // the UART hardware buffer (64 bytes) can overflow and we drop NMEA data.
    bool newGpsSentence = _gps.update();

    // ---- IMU predict step ----
    // Run as fast as I2C allows (~1ms per read at 400kHz).
    // We throttle to a minimum interval to avoid swamping the I2C bus.
    // 10ms = 100 Hz: matches the MPU-6050's default output rate.
    _runImuPredict();

    // ---- GPS update step ----
    // Only feed a new measurement to the Kalman filter when GPS has parsed
    // a fresh sentence. Feeding the same GPS speed twice is incorrect -
    // it would make the filter over-confident in stale data.
    if (newGpsSentence) {
        _runGpsUpdate();
    }

    // ---- Build the FusedData snapshot ----
    GPSData gd = _gps.getData();
    IMUData id = _imu.getData();

    _data.fusedSpeedKmh       = _kf.getSpeedKmh();
    _data.gpsSpeedKmh         = gd.speedKmh;
    _data.imuAccelMs2         = _imu.getLongitudinalAccel();
    _data.imuAccelRaw         = id.accelX;
    _data.hasFix              = gd.hasFix;
    _data.isStationary        = gd.isStationary;
    _data.lat                 = gd.lat;
    _data.lon                 = gd.lon;
    _data.hdop                = gd.hdop;
    _data.satellites          = gd.satellites;
    _data.kalmanGain          = _kf.getKalmanGain();
    _data.kalmanCovariance    = _kf.getCovariance();
    _data.gpsUpdatedThisCycle = newGpsSentence;
    _data.timestampMs         = now;
}

// ---- _runImuPredict() ----
void SensorFusion::_runImuPredict() {
    uint32_t now = millis();
    uint32_t elapsed = now - _lastImuMs;

    // Skip if less than 10ms since last IMU read (100 Hz ceiling)
    if (elapsed < 10) return;

    if (!_imu.update()) {
        // I2C read failed: skip this predict step rather than feeding bad data
        return;
    }

    float dt      = elapsed / 1000.0f;    // convert ms → seconds
    float accel   = _imu.getLongitudinalAccel();

    // If GPS says we're stationary, don't let IMU noise drift the speed estimate.
    // IMU noise at rest is ~±0.03 m/s² (from Phase 3 data). Over 10ms that's
    // only 0.0003 m/s², but it accumulates over time. The stationary lock
    // prevents this from inflating the fused speed when parked.
    if (_gps.isStationary()) {
        _kf.forceStationary();
    } else {
        _kf.predict(accel, dt);
    }

    _lastImuMs = now;
}

// ---- _runGpsUpdate() ----
void SensorFusion::_runGpsUpdate() {
    GPSData gd = _gps.getData();

    // Only update Kalman if GPS fix is valid and device is not stationary.
    // If stationary, forceStationary() in _runImuPredict handles it.
    if (!gd.hasFix) {
        LOG_DEBUG("FUSION", "No GPS fix: skipping Kalman update");
        return;
    }

    if (gd.isStationary) {
        _kf.forceStationary();
        return;
    }

    // Convert km/h → m/s before passing to Kalman filter
    // (filter state is in m/s; km/h output is done at getSpeedKmh())
    float gpsMs = gd.rawSpeedKmh / 3.6f;
    _kf.update(gpsMs);

    LOG_DEBUG("FUSION", "GPS→KF: %.2f km/h  fused=%.2f km/h  K=%.3f",
              gd.rawSpeedKmh, _kf.getSpeedKmh(), _kf.getKalmanGain());
}

// ---- CSV logging ----
void SensorFusion::printCsvHeader() {
    // Superset of KalmanSpeedFilter CSV: includes GPS and IMU raw data
    Serial.println(F("ms,hasFix,sats,hdop,gps_kmh,imu_accel_ms2,fused_kmh,"
                     "kalman_gain,covariance,isStationary,gps_updated,lat,lon"));
}

void SensorFusion::printCsvRow() const {
    Serial.printf("%lu,%d,%d,%.2f,%.2f,%.4f,%.2f,%.4f,%.4f,%d,%d,%.6f,%.6f\n",
        _data.timestampMs,
        (int)_data.hasFix,
        _data.satellites,
        _data.hdop,
        _data.gpsSpeedKmh,
        _data.imuAccelMs2,
        _data.fusedSpeedKmh,
        _data.kalmanGain,
        _data.kalmanCovariance,
        (int)_data.isStationary,
        (int)_data.gpsUpdatedThisCycle,
        _data.lat,
        _data.lon
    );
}