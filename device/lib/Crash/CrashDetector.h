// ============================================================
// lib/Crash/CrashDetector.h: impact + confirmation crash detection
//
// IDLE ──|a| > impactG while moving──► CANDIDATE
// CANDIDATE ──tilted > tiltDeg for 2 s──► CRASH   (two-wheeler fell over)
//           ──moving fast before, ~stopped 3 s after──► CRASH   (sudden stop)
//           ──8 s with neither──► IDLE   (pothole, kerb, dropped phone mount)
//
// The device must be fixed to the vehicle: orientation is compared with
// the gravity direction learned while riding normally before the impact.
// ============================================================
#pragma once
#include <Arduino.h>
#include "VehicleConfig.h"

struct CrashInfo {
    float    peakG;
    float    tiltDeg;
    float    speedBeforeKmh;
    const char* reason;   // "tilt" or "stop"
};

class CrashDetector {
public:
    // ax/ay/az: raw acceleration including gravity, m/s². Call once per IMU sample.
    // Returns true exactly once, on the sample a crash is confirmed.
    bool update(float ax, float ay, float az, float speedKmh, const CrashThresholds& t);
    const CrashInfo& last() const { return _info; }
    bool candidateOpen() const { return _state == State::CANDIDATE; }

private:
    enum class State : uint8_t { IDLE, CANDIDATE };
    State    _state = State::IDLE;
    float    _ref[3]  = {0, 0, 1};   // slow gravity direction (unit vector)
    float    _cur[3]  = {0, 0, 1};   // fast gravity direction (unit vector)
    bool     _refValid = false;
    float    _recentMaxSpeed = 0;    // decays, so it means "speed in the last few seconds"
    uint32_t _lastMs = 0;
    uint32_t _impactMs = 0;
    uint32_t _tiltSinceMs = 0;
    float    _frozenRef[3] = {0, 0, 1};
    CrashInfo _info = {};

    static float _angleDeg(const float a[3], const float b[3]);
};
