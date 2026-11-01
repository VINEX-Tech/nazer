#include <Wire.h>
// ============================================================
// IMU.cpp: MPU-6050 driver implementation for NAZER
//
// Matches IMU.h interface exactly.
// Reads accelerometer only (no gyro: gyro drifts over time).
// Calibration: 1000 samples averaged, offsets saved to NVS.
// NVS namespace: "nazer_imu": survives reboot.
//
// Phase 3 goal: verify ax≈0, ay≈0, az≈0 after calibration when flat.
// Phase 4 will use getLongitudinalAccel() for Kalman fusion.
// ============================================================

#include "IMU.h"
#include <Preferences.h>   // ESP32 NVS wrapper
#include <math.h>          // atan2f, sqrtf

// ---- MPU-6050 register addresses ----
// We only need a handful: the chip has 117 registers total.
static constexpr uint8_t MPU_ADDR          = 0x68;   // I2C address (AD0=LOW)
static constexpr uint8_t REG_PWR_MGMT_1    = 0x6B;   // Power management 1
static constexpr uint8_t REG_ACCEL_CONFIG  = 0x1C;   // Accelerometer full-scale range
static constexpr uint8_t REG_ACCEL_XOUT_H  = 0x3B;   // First of 6 accel data bytes
static constexpr uint8_t REG_WHO_AM_I      = 0x75;   // Should read 0x68

// ---- Constructor ----
IMU::IMU() {
    // Zero-initialise the data struct.
    // C++ guarantees POD members are NOT zero-init by default in stack objects.
    memset(&_data, 0, sizeof(_data));
    _data.calibrated = false;
    _data.dataReady  = false;
}

// ============================================================
// begin(): call once in setup()
// Initialises the MPU-6050 and attempts to load saved calibration.
// Returns false if the chip is not detected on the I2C bus.
// ============================================================
bool IMU::begin() {
    // WHO_AM_I register check: confirms chip is present and not fried.
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_WHO_AM_I);
    if (Wire.endTransmission(false) != 0) {
        // I2C transmission failed: no ACK from device
        Serial.println("[E][IMU] MPU-6050 NOT FOUND: check SDA=21/SCL=22 wiring and 3.3V power");
        return false;
    }
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
    uint8_t whoAmI = Wire.read();

    // MPU-6050 returns 0x68, MPU-6500 returns 0x70, MPU-9250 returns 0x71.
    // We accept 0x68 only: that's the GY-521 module we have.
    if (whoAmI != 0x68) {
        Serial.printf("[E][IMU] WHO_AM_I = 0x%02X: expected 0x68. Wrong chip?\n", whoAmI);
        return false;
    }

    Serial.printf("[I][IMU] MPU-6050 found at 0x%02X\n", MPU_ADDR);

    _wakeUp();          // Chip boots in sleep mode: must wake it
    _configureAccel();  // Set ±16g full-scale range

    // Try to load saved calibration from NVS.
    // If none exists, the user must trigger calibrate() manually.
    if (_loadFromNVS()) {
        Serial.println("[I][IMU] Calibration loaded from NVS");
    } else {
        Serial.println("[W][IMU] No saved calibration: place device flat and press encoder button to calibrate");
    }

    return true;
}

// ============================================================
// update(): call every loop() iteration
// Reads one accelerometer sample, applies calibration, updates tilt.
// Returns true if a new sample was read successfully.
// ============================================================
bool IMU::update() {
    int16_t rx, ry, rz;

    if (!_readRaw(rx, ry, rz)) {
        _data.dataReady = false;
        return false;
    }

    // Convert raw int16 to m/s²
    // Full-scale ±16g → LSB sensitivity = 2048 counts per g
    // 1g = 9.81 m/s²  →  scale = 9.81 / 2048
    _data.rawX = rx * ACCEL_SCALE;
    _data.rawY = ry * ACCEL_SCALE;
    _data.rawZ = rz * ACCEL_SCALE;

    // Raw magnitude includes gravity (should be ≈9.81 when flat and still)
    _data.rawMagnitude = sqrtf(
        _data.rawX * _data.rawX +
        _data.rawY * _data.rawY +
        _data.rawZ * _data.rawZ
    );

    // Store offsets in data struct for CSV logging visibility
    _data.offsetX = _data.calibrated ? _data.offsetX : 0.0f;
    _data.offsetY = _data.calibrated ? _data.offsetY : 0.0f;
    _data.offsetZ = _data.calibrated ? _data.offsetZ : 0.0f;

    // Apply calibration: subtract offsets that were measured while flat+still.
    // After this, ax≈0, ay≈0, az≈0 when flat (gravity is in the offset).
    _applyCalibration();

    // Calculate tilt angles from calibrated data.
    _updateTilt();

    _data.dataReady = true;
    return true;
}

// ============================================================
// calibrate(): blocking, ~3 seconds
// Device MUST be flat and completely still.
// Accumulates 1000 raw samples, averages them, saves to NVS.
//
// The offset removes gravity from Z as well: so after calibration:
//   ax_cal = rawX - offsetX  ≈ 0   (no lateral force when flat)
//   ay_cal = rawY - offsetY  ≈ 0   (no longitudinal force when flat)
//   az_cal = rawZ - offsetZ  ≈ 0   (gravity baked into offsetZ)
//
// Why remove gravity from Z?
//   The Kalman fusion uses longitudinal acceleration (X axis).
//   We want that to read 0 when stationary, not 9.81 when stationary.
//   Removing offsetZ from Z keeps all axes consistent.
// ============================================================
void IMU::calibrate() {
    Serial.println("[I][IMU] Starting calibration: keep device FLAT and STILL");
    Serial.printf("[I][IMU] Collecting %d samples...\n", CAL_SAMPLES);

    double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
    int goodSamples = 0;

    for (int i = 0; i < CAL_SAMPLES; i++) {
        int16_t rx, ry, rz;
        if (_readRaw(rx, ry, rz)) {
            sumX += rx;
            sumY += ry;
            sumZ += rz;
            goodSamples++;
        }

        // Report progress every 100 samples
        if (i % 100 == 0) {
            uint8_t pct = (uint8_t)((i * 100) / CAL_SAMPLES);
            Serial.printf("[I][IMU] Calibrating... %d%%\n", pct);
            if (_progressCallback) _progressCallback(pct);
        }

        // Spacing between samples: 3ms → total ≈ 3 seconds
        // MPU-6050 default output rate is 1kHz (1ms), so 3ms is fine.
        delay((uint32_t)CAL_SAMPLE_MS);
    }

    if (goodSamples < CAL_SAMPLES / 2) {
        Serial.printf("[E][IMU] Calibration failed: only %d/%d samples read\n", goodSamples, CAL_SAMPLES);
        return;
    }

    // Average the raw integer sums, then convert to m/s²
    // These offsets represent the sensor reading when it SHOULD be measuring 0
    // (i.e., no acceleration on X/Y, and gravity on Z)
    _data.offsetX = (float)(sumX / goodSamples) * ACCEL_SCALE;
    _data.offsetY = (float)(sumY / goodSamples) * ACCEL_SCALE;
    _data.offsetZ = (float)(sumZ / goodSamples) * ACCEL_SCALE;
    // Note: offsetZ will be ≈+9.81 (gravity). We subtract it during _applyCalibration()
    // so az_calibrated ≈ 0 when flat and still.

    _data.calibrated = true;

    if (_progressCallback) _progressCallback(100);

    Serial.printf("[I][IMU] Calibration complete (%d samples)\n", goodSamples);
    Serial.printf("[I][IMU]   offsetX = %.4f m/s²\n", _data.offsetX);
    Serial.printf("[I][IMU]   offsetY = %.4f m/s²\n", _data.offsetY);
    Serial.printf("[I][IMU]   offsetZ = %.4f m/s² (includes gravity ≈9.81)\n", _data.offsetZ);

    if (_saveToNVS()) {
        Serial.println("[I][IMU] Calibration saved to NVS: survives reboot");
    } else {
        Serial.println("[E][IMU] Failed to save calibration to NVS");
    }
}

// ============================================================
// clearCalibration(): erases NVS and resets offsets to zero
// ============================================================
void IMU::clearCalibration() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();

    _data.offsetX    = 0.0f;
    _data.offsetY    = 0.0f;
    _data.offsetZ    = 0.0f;
    _data.calibrated = false;

    Serial.println("[I][IMU] Calibration erased from NVS");
}

// ============================================================
// printCsvHeader(): static, call once before the loop
// ============================================================
void IMU::printCsvHeader() {
    Serial.println("ms,rawX,rawY,rawZ,rawMag,calX,calY,calZ,tiltX_deg,tiltY_deg,calibrated");
}

// ============================================================
// printCsvRow(): call every loop() after update()
// ============================================================
void IMU::printCsvRow() const {
    Serial.printf("%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%d\n",
        millis(),
        _data.rawX, _data.rawY, _data.rawZ,
        _data.rawMagnitude,
        _data.accelX, _data.accelY, _data.accelZ,
        _data.tiltX, _data.tiltY,
        (int)_data.calibrated
    );
}

// ============================================================
// _readRaw(): reads 6 bytes from ACCEL_XOUT_H register burst
// MPU-6050 packs XH, XL, YH, YL, ZH, ZL in consecutive registers.
// Returns false if I2C read fails.
// ============================================================
bool IMU::_readRaw(int16_t& rx, int16_t& ry, int16_t& rz) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;   // I2C error

    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6);
    if (Wire.available() < 6) return false;               // Short read

    // Each axis is a 16-bit signed integer, big-endian (high byte first)
    rx = (int16_t)((Wire.read() << 8) | Wire.read());
    ry = (int16_t)((Wire.read() << 8) | Wire.read());
    rz = (int16_t)((Wire.read() << 8) | Wire.read());
    return true;
}

// ============================================================
// _applyCalibration(): subtracts stored offsets from raw values
// After this: accelX/Y/Z ≈ 0 when flat and still.
// If not calibrated, accelX/Y/Z = rawX/Y/Z (pass through unchanged).
// ============================================================
void IMU::_applyCalibration() {
    if (_data.calibrated) {
        _data.accelX = _data.rawX - _data.offsetX;
        _data.accelY = _data.rawY - _data.offsetY;
        _data.accelZ = _data.rawZ - _data.offsetZ;
    } else {
        // No calibration yet: expose raw values directly
        _data.accelX = _data.rawX;
        _data.accelY = _data.rawY;
        _data.accelZ = _data.rawZ;
    }
}

// ============================================================
// _updateTilt(): computes pitch and roll from calibrated accel
//
// tiltX (pitch): rotation around Y axis: front/back tilt
// tiltY (roll):  rotation around X axis: left/right tilt
//
// These are correct for small tilt angles (< ±60°).
// For larger angles, a full 3-axis atan2 is needed.
// We only use this for verifying device is reasonably flat in the vehicle.
// ============================================================
void IMU::_updateTilt() {
    // Use raw values for tilt: calibrated accel removes gravity which
    // is the signal we need for tilt calculation.
    // When flat: rawX≈0, rawY≈0, rawZ≈9.81
    // tiltX = atan2(rawX, rawZ): pitch
    // tiltY = atan2(rawY, rawZ): roll
    _data.tiltX = atan2f(_data.rawX, _data.rawZ) * 180.0f / (float)M_PI;
    _data.tiltY = atan2f(_data.rawY, _data.rawZ) * 180.0f / (float)M_PI;
}

// ============================================================
// _saveToNVS(): stores three float offsets + validity flag
// Uses ESP32 Preferences library (NVS key-value store in flash).
// ============================================================
bool IMU::_saveToNVS() {
    Preferences prefs;
    // false = read/write mode
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[E][IMU] NVS open failed: cannot save calibration");
        return false;
    }

    prefs.putFloat(NVS_KEY_OX, _data.offsetX);
    prefs.putFloat(NVS_KEY_OY, _data.offsetY);
    prefs.putFloat(NVS_KEY_OZ, _data.offsetZ);
    prefs.putBool(NVS_KEY_VALID, true);
    prefs.end();
    return true;
}

// ============================================================
// _loadFromNVS(): retrieves calibration offsets saved by _saveToNVS()
// Returns false if no calibration has ever been saved.
// ============================================================
bool IMU::_loadFromNVS() {
    Preferences prefs;
    // true = read-only mode (safer: won't create namespace if absent)
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        // Namespace doesn't exist yet: first boot, no calibration saved
        return false;
    }

    bool valid = prefs.getBool(NVS_KEY_VALID, false);
    if (!valid) {
        prefs.end();
        return false;
    }

    _data.offsetX    = prefs.getFloat(NVS_KEY_OX, 0.0f);
    _data.offsetY    = prefs.getFloat(NVS_KEY_OY, 0.0f);
    _data.offsetZ    = prefs.getFloat(NVS_KEY_OZ, 0.0f);
    _data.calibrated = true;
    prefs.end();

    Serial.printf("[I][IMU]   Loaded offsets: X=%.4f Y=%.4f Z=%.4f m/s²\n",
        _data.offsetX, _data.offsetY, _data.offsetZ);
    return true;
}

// ============================================================
// _wakeUp(): the MPU-6050 powers on in sleep mode (PWR_MGMT_1 bit6=1)
// Writing 0x00 to PWR_MGMT_1 wakes it and selects internal 8MHz oscillator.
// ============================================================
void IMU::_wakeUp() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_PWR_MGMT_1);
    Wire.write(0x00);   // Clear SLEEP bit: wake up, use internal oscillator
    Wire.endTransmission(true);
    delay(100);         // Give the chip 100ms to stabilise after wakeup
    Serial.println("[I][IMU] MPU-6050 woken from sleep");
}

// ============================================================
// _configureAccel(): sets full-scale range to ±16g
// ACCEL_CONFIG register: bits [4:3] = AFS_SEL
//   00 → ±2g  (16384 LSB/g)
//   01 → ±4g  (8192 LSB/g)
//   10 → ±8g  (4096 LSB/g)
//   11 → ±16g (2048 LSB/g) ← we use this
//
// ±16g keeps crash impacts (often 4-10g+) from clipping. Resolution is
// still 0.5 mg, far finer than vehicle acceleration (0–4 m/s²) needs.
// ============================================================
void IMU::_configureAccel() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_CONFIG);
    Wire.write(0x18);   // AFS_SEL = 11 → ±16g
    Wire.endTransmission(true);
    Serial.println("[I][IMU] Accelerometer configured: ±16g full-scale");
}