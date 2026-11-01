#pragma once
// ============================================================
// IMU.h: MPU-6050 driver for NAZER
//
// Hardware:
//   GY-521 module (MPU-6050) on shared I2C bus
//   SDA = GPIO21, SCL = GPIO22, Address = 0x68 (AD0 LOW)
//
// What this module does:
//   - Reads raw accelerometer data from MPU-6050 via I2C
//   - Runs a calibration routine (1000 samples, ~3 seconds)
//   - Saves calibration offsets to NVS flash (survives reboot)
//   - Exposes calibrated acceleration in m/s² with gravity removed
//   - Calculates longitudinal acceleration for Kalman fusion (Phase 4)
//
// What we do NOT use:
//   - Gyroscope (drifts unacceptably over minutes: not useful for speed)
//   - DMP (Digital Motion Processor): overkill for 1D speed fusion
//   - Temperature compensation: small effect, skip for now
//
// Usage:
//   IMU imu;
//   imu.begin();               // call once in setup()
//   imu.update();              // call every loop(): reads one sample
//   IMUData d = imu.getData();
//   float ax = d.accelX;      // m/s², gravity removed
// ============================================================

#include <Arduino.h>


// ---- Data structure returned to callers ----
struct IMUData {
    // Calibrated acceleration in m/s² (gravity removed on Z axis)
    float accelX;       // longitudinal: positive = forward acceleration
    float accelY;       // lateral: positive = left
    float accelZ;       // vertical: positive = up (≈0 after gravity removal)

    // Raw acceleration in m/s² (before calibration offset applied)
    float rawX, rawY, rawZ;

    // Vector magnitude: should be ~9.81 when flat and still (gravity)
    // After calibration applied: should be ~0 when flat (gravity removed from all axes)
    // We keep the raw magnitude for diagnostics
    float rawMagnitude;

    // Calibration offsets (loaded from NVS or calculated by calibrate())
    float offsetX, offsetY, offsetZ;

    // Tilt angles in degrees (calculated from calibrated accel)
    // Useful for verifying the device is mounted reasonably flat
    float tiltX;   // pitch: rotation around Y axis
    float tiltY;   // roll: rotation around X axis

    // State flags
    bool calibrated;     // true if offsets have been applied
    bool dataReady;      // true if a fresh sample was read this update()
};

// ============================================================
class IMU {
public:
    IMU();

    // Call once in setup(). Initialises I2C and configures MPU-6050.
    // Attempts to load calibration from NVS automatically.
    // Returns true if MPU-6050 was found and initialised.
    bool begin();

    // Call every loop() iteration. Reads one accelerometer sample.
    // Applies calibration offsets if available.
    // Returns true if a new sample was read successfully.
    bool update();

    // Returns a copy of the latest IMU data.
    IMUData getData() const { return _data; }

    // ---- Calibration ----
    // Runs the calibration routine synchronously (blocking ~3 seconds).
    // Device MUST be flat and still during this call.
    // Saves result to NVS automatically.
    // Call from loop() only when triggered by user (encoder button press).
    void calibrate();

    // True if offsets are loaded (either from NVS or fresh calibration).
    bool isCalibrated() const { return _data.calibrated; }

    // Erase saved calibration from NVS (forces re-calibration on next boot).
    void clearCalibration();

    // ---- For Kalman fusion (Phase 4) ----
    // Returns longitudinal acceleration in m/s².
    // This is the component of acceleration along the vehicle's direction of travel.
    // Simple approximation: use X axis (assumes device is mounted facing forward).
    // Will be improved with heading data in Phase 4.
    float getLongitudinalAccel() const { return _data.calibrated ? _data.accelX : 0.0f; }

    // CSV logging helpers
    static void printCsvHeader();
    void printCsvRow() const;

    // Called by test sketch to show progress on OLED during calibration
    // (callback receives 0–100 percent)
    void setCalibrationProgressCallback(void (*cb)(uint8_t pct)) {
        _progressCallback = cb;
    }

private:
    IMUData _data;

    // MPU-6050 raw register values are int16_t in range -32768 to +32767.
    // Full-scale range ±16g → scale factor = 9.81 / 2048.0
    static constexpr float ACCEL_SCALE = 9.81f / 2048.0f;   // ±16g full-scale (crash detection needs headroom)

    // NVS key names for saved calibration
    static constexpr const char* NVS_NAMESPACE = "nazer_imu";
    static constexpr const char* NVS_KEY_OX    = "off_x";
    static constexpr const char* NVS_KEY_OY    = "off_y";
    static constexpr const char* NVS_KEY_OZ    = "off_z";
    static constexpr const char* NVS_KEY_VALID = "cal_ok";

    static constexpr int   CAL_SAMPLES = 1000;   // samples to average for calibration
    static constexpr float CAL_SAMPLE_MS = 3.0f; // ms between calibration samples (≈3s total)

    // Optional progress callback for the test sketch
    void (*_progressCallback)(uint8_t pct) = nullptr;

    // ---- Internal helpers ----
    bool _readRaw(int16_t& rx, int16_t& ry, int16_t& rz);
    void _applyCalibration();
    void _updateTilt();
    bool _saveToNVS();
    bool _loadFromNVS();

    // Wake the MPU-6050 from sleep (it boots in sleep mode)
    void _wakeUp();

    // Configure accelerometer full-scale range to ±16g
    void _configureAccel();
};
