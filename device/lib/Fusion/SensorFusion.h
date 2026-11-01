#pragma once
// ============================================================
// SensorFusion.h: GPS + IMU Sensor Fusion Coordinator
//
// This class ties together the GPS module, IMU module, and
// KalmanSpeedFilter into a single, clean interface.
//
// Responsibilities:
//   - Runs GPS and IMU update loops at their natural rates
//   - Feeds IMU accel into Kalman predict at ~100 Hz
//   - Feeds GPS speed into Kalman update only when a NEW sentence arrives
//   - Applies stationary lock: if GPS says parked, force speed to 0
//   - Exposes fused speed, violation state, and raw sensor data
//
// Why separate from the Kalman filter class?
//   KalmanSpeedFilter is a pure math class: no Arduino dependencies.
//   SensorFusion owns the timing, sensor polling, and state machine.
//   This keeps each class unit-testable in isolation.
//
// Usage:
//   SensorFusion fusion;
//   fusion.begin();
//   // in loop():
//   fusion.update();
//   float speed = fusion.getFusedSpeedKmh();
//   bool  over  = fusion.isSpeedingBy(speedLimit);
// ============================================================

#include <Arduino.h>
#include "../GPS/GPS.h"
#include "../IMU/IMU.h"
#include "KalmanSpeedFilter.h"

// Fused data snapshot: everything the display / cloud client needs
struct FusedData {
    // Fused speed (the primary output)
    float fusedSpeedKmh;

    // Raw inputs for comparison / CSV logging
    float gpsSpeedKmh;
    float imuAccelMs2;      // longitudinal acceleration from IMU
    float imuAccelRaw;      // same value: kept for clarity in logs

    // GPS metadata
    bool  hasFix;
    bool  isStationary;
    double lat;
    double lon;
    float  hdop;
    uint8_t satellites;

    // Kalman diagnostics
    float kalmanGain;
    float kalmanCovariance;
    bool  gpsUpdatedThisCycle;  // true if GPS fed a new measurement this loop

    // Timing
    uint32_t timestampMs;
};

// ============================================================
class SensorFusion {
public:
    SensorFusion();

    // Call once in setup(). Starts GPS UART and configures IMU.
    // Wire.begin() must be called before this (for IMU I2C).
    // Returns false if IMU init fails (GPS failure is non-fatal: keep waiting for fix).
    bool begin();

    // Call every loop() iteration: as fast as possible.
    // Internally handles all timing: IMU runs at ~100 Hz, GPS at ~1 Hz.
    // Updates internal FusedData struct.
    void update();

    // ---- Primary output ----
    float getFusedSpeedKmh()  const { return _data.fusedSpeedKmh; }
    float getGpsSpeedKmh()    const { return _data.gpsSpeedKmh; }
    float getImuAccelMs2()    const { return _data.imuAccelMs2; }

    // Is the vehicle exceeding the speed limit by more than VIOLATION_MARGIN_KMH?
    bool  isSpeedingBy(float limitKmh) const {
        return _data.fusedSpeedKmh > (limitKmh + VIOLATION_MARGIN_KMH);
    }

    // ---- GPS state ----
    bool   hasFix()       const { return _data.hasFix; }
    bool   isStationary() const { return _data.isStationary; }
    double getLat()       const { return _data.lat; }
    double getLon()       const { return _data.lon; }

    // ---- Full snapshot ----
    FusedData getData() const { return _data; }

    // Access underlying modules if needed (e.g. for display or calibration)
    GPS& gps()  { return _gps; }
    IMU& imu()  { return _imu; }

    // ---- IMU calibration pass-through ----
    // Call from main sketch when user triggers calibration via encoder button.
    void calibrateImu() { _imu.calibrate(); }
    bool imuCalibrated() const { return _imu.isCalibrated(); }

    // ---- CSV logging ----
    static void printCsvHeader();
    void printCsvRow() const;

private:
    GPS               _gps;
    IMU               _imu;
    KalmanSpeedFilter _kf;
    FusedData         _data;

    // Timing
    uint32_t _lastImuMs;    // millis() of last IMU sample
    uint32_t _lastGpsMs;    // millis() of last GPS sentence
    uint32_t _lastCsvMs;    // millis() of last CSV print

    // Track GPS sentence count to detect when a NEW sentence arrives
    uint32_t _lastGpsSentenceCount;

    void _runImuPredict();
    void _runGpsUpdate();
};