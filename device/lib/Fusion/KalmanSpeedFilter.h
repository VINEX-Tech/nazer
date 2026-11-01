#pragma once
// ============================================================
// KalmanSpeedFilter.h: 1D Kalman filter for speed estimation
//
// Fuses two measurements:
//   - GPS speed    (accurate but slow: ~1 Hz, latency ~200ms)
//   - IMU accel    (fast: ~100 Hz, but drifts if integrated)
//
// The filter is a classic 1D kinematic Kalman filter:
//   State:    x = speed (m/s)
//   Control:  u = longitudinal acceleration (m/s²) from IMU
//   Measurement: z = speed (m/s) from GPS
//
// Predict step (runs at IMU rate, ~100 Hz):
//   x_pred = x + u * dt          (integrate acceleration)
//   P_pred = P + Q               (grow uncertainty)
//
// Update step (runs only when new GPS speed arrives, ~1 Hz):
//   K      = P_pred / (P_pred + R_gps)
//   x      = x_pred + K * (z - x_pred)
//   P      = (1 - K) * P_pred
//
// Tuning parameters (all in config.h):
//   KALMAN_Q: process noise: how much we trust IMU prediction
//                  higher Q = filter adapts faster, more noisy
//                  lower Q  = filter smoother, slower to respond
//   KALMAN_R_GPS: GPS measurement noise variance
//                  set from Phase 2 standstill data: max noise 0.09 km/h
//                  → 0.025 m/s → variance ≈ 0.0006, but use 0.5 conservatively
//   KALMAN_R_IMU: not used directly in this filter (IMU enters via predict step)
//                  kept in config.h for future extension
//
// Usage:
//   KalmanSpeedFilter kf;
//   kf.begin(initialSpeedMs);                  // call once
//   kf.predict(imuAccelMs2, dtSeconds);        // call every IMU sample
//   kf.update(gpsSpeedMs);                     // call only when GPS has new data
//   float speed = kf.getSpeedKmh();
// ============================================================

#include <Arduino.h>
#include "../../include/config.h"

class KalmanSpeedFilter {
public:
    KalmanSpeedFilter();

    // Initialise filter state.
    // initialSpeedMs: starting estimate in m/s (use 0.0 if unknown).
    // Call once before the first predict/update cycle.
    void begin(float initialSpeedMs = 0.0f);

    // ---- Predict step: call at IMU rate (~100 Hz) ----
    // accelMs2: longitudinal acceleration in m/s² (from IMU, gravity removed)
    // dtSeconds: time since last predict call in seconds
    //
    // Integrates acceleration into the speed estimate and grows uncertainty.
    // Even without GPS, the filter gives a reasonable short-term speed estimate.
    void predict(float accelMs2, float dtSeconds);

    // ---- Update step: call only when GPS has a fresh speed measurement ----
    // gpsSpeedMs: GPS speed in m/s (convert from km/h before calling)
    //
    // Corrects the predicted speed using GPS, weighted by Kalman gain.
    // High R_gps → filter trusts GPS less, stays closer to IMU prediction.
    // Low R_gps  → filter snaps to GPS quickly.
    void update(float gpsSpeedMs);

    // ---- Output ----
    // Returns fused speed in km/h (converts internally from m/s state).
    float getSpeedKmh() const { return _x * 3.6f; }

    // Returns fused speed in m/s (raw state variable).
    float getSpeedMs()  const { return _x; }

    // Returns current estimate uncertainty (state covariance P).
    // Useful for diagnostics: high P means less confident estimate.
    float getCovariance() const { return _P_cov; }

    // Returns last computed Kalman gain (0–1).
    // Near 1 = trusted GPS heavily. Near 0 = trusted IMU prediction.
    float getKalmanGain() const { return _K; }

    // ---- Stationary lock ----
    // When GPS reports stationary (speed below threshold), force state to 0.
    // This prevents IMU noise from slowly drifting the speed estimate upward.
    // Call this instead of predict/update when GPS.isStationary() is true.
    void forceStationary();

    // ---- Tuning helpers ----
    // Override Q/R values at runtime without recompiling.
    // Useful for live tuning during the Phase 4 drive test.
    void setProcessNoise(float q)       { _Q = q; }
    void setGpsMeasurementNoise(float r){ _R_gps = r; }

    // Reset filter to a new speed (e.g. after GSM speed limit update changes context)
    void reset(float speedMs = 0.0f);

    // CSV logging
    static void printCsvHeader();
    void printCsvRow(float gpsKmh, float imuAccel, bool gpsUpdated) const;

private:
    float _x;       // State: estimated speed in m/s
    float _P_cov;       // Estimate covariance (uncertainty)
    float _K;       // Last Kalman gain (stored for diagnostics)
    float _Q;       // Process noise variance (from config.h KALMAN_Q)
    float _R_gps;   // GPS measurement noise variance (from config.h KALMAN_R_GPS)
};