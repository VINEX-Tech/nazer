// ============================================================
// KalmanSpeedFilter.cpp: Implementation
// ============================================================

#include "KalmanSpeedFilter.h"
#include "../Logger/Logger.h"

// ---- Constructor ----
KalmanSpeedFilter::KalmanSpeedFilter()
    : _x(0.0f)
    , _P_cov(1.0f)    // Initial covariance: high uncertainty until first GPS update
    , _K(0.0f)
    , _Q(KALMAN_Q)
    , _R_gps(KALMAN_R_GPS)
{
}

// ---- begin() ----
void KalmanSpeedFilter::begin(float initialSpeedMs) {
    _x = initialSpeedMs;
    _P_cov = 1.0f;     // Start with moderate uncertainty
    _K = 0.0f;
    _Q = KALMAN_Q;
    _R_gps = KALMAN_R_GPS;

    LOG_INFO("KF", "Kalman filter init: speed=%.3f m/s  Q=%.4f  R_gps=%.4f",
             _x, _Q, _R_gps);
}

// ---- predict(): IMU step ----
// Called at ~100 Hz whenever a new IMU sample is available.
//
// Physics: new_speed = old_speed + acceleration * dt
// This is the "motion model": we predict where the state moved
// based on what the IMU measured.
//
// The covariance P grows by Q each step because we trust the model
// imperfectly (IMU has noise, our dt isn't perfectly constant, etc.)
void KalmanSpeedFilter::predict(float accelMs2, float dtSeconds) {
    // Guard: reject unreasonably large dt (e.g. first call after long delay)
    // This prevents the speed estimate jumping wildly on startup.
    if (dtSeconds <= 0.0f || dtSeconds > 0.5f) {
        return;
    }

    // State prediction: integrate acceleration
    _x = _x + accelMs2 * dtSeconds;

    // Clamp: speed cannot be negative (car can't go backwards in this context)
    // If braking hard and the filter overshoots, snap to 0.
    if (_x < 0.0f) _x = 0.0f;

    // Covariance prediction: uncertainty grows with each predict step
    // More process noise (higher Q) = filter reacts faster to acceleration
    _P_cov = _P_cov + _Q;
}

// ---- update(): GPS measurement step ----
// Called only when GPS has a fresh speed reading (~1 Hz).
//
// The Kalman gain K determines how much we trust GPS vs our prediction:
//   K = P / (P + R_gps)
//   If P is large (high uncertainty) → K approaches 1 → trust GPS more
//   If P is small (confident prediction) → K approaches 0 → trust IMU more
//
// After updating, covariance P shrinks because we've incorporated new info.
void KalmanSpeedFilter::update(float gpsSpeedMs) {
    // Guard: reject clearly invalid GPS speed
    if (gpsSpeedMs < 0.0f || gpsSpeedMs > 300.0f / 3.6f) {
        LOG_WARN("KF", "GPS speed out of range: %.2f m/s: skipping update", gpsSpeedMs);
        return;
    }

    // Kalman gain
    _K = _P_cov / (_P_cov + _R_gps);

    // Innovation (measurement residual): how far GPS is from our prediction
    float innovation = gpsSpeedMs - _x;

    // State update: blend prediction with GPS measurement
    _x = _x + _K * innovation;

    // Covariance update: certainty improves after a measurement
    _P_cov = (1.0f - _K) * _P_cov;

    // Clamp speed to non-negative
    if (_x < 0.0f) _x = 0.0f;

    LOG_DEBUG("KF", "GPS update: gps=%.2f m/s  pred=%.2f  innov=%.3f  K=%.3f  new_spd=%.2f  P=%.4f",
              gpsSpeedMs, _x - _K * innovation, innovation, _K, _x, _P_cov);
}

// ---- forceStationary() ----
// When GPS definitively says we're parked, slam the speed to zero.
// Also reset P slightly so the filter doesn't fight GPS hard on first movement.
void KalmanSpeedFilter::forceStationary() {
    _x = 0.0f;
    _P_cov = 0.5f;   // Moderate confidence: we know speed is 0, but IMU may drift quickly
}

// ---- reset() ----
void KalmanSpeedFilter::reset(float speedMs) {
    _x = speedMs;
    _P_cov = 1.0f;
    _K = 0.0f;
    LOG_INFO("KF", "Filter reset to %.2f m/s", speedMs);
}

// ---- CSV helpers ----
void KalmanSpeedFilter::printCsvHeader() {
    Serial.println(F("ms,gps_kmh,imu_accel_ms2,fused_kmh,kalman_gain,covariance,gps_updated"));
}

void KalmanSpeedFilter::printCsvRow(float gpsKmh, float imuAccel, bool gpsUpdated) const {
    Serial.printf("%lu,%.2f,%.4f,%.2f,%.4f,%.4f,%d\n",
        millis(),
        gpsKmh,
        imuAccel,
        getSpeedKmh(),
        _K,
        _P_cov,
        (int)gpsUpdated
    );
}