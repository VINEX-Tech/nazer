#include "CrashDetector.h"
#include <math.h>

static constexpr float G = 9.81f;
static constexpr uint32_t CANDIDATE_TIMEOUT_MS = 8000;
static constexpr uint32_t TILT_HOLD_MS         = 2000;
static constexpr uint32_t STOP_CHECK_AFTER_MS  = 3000;
static constexpr float    STOPPED_KMH          = 5.0f;
static constexpr float    MIN_MOVING_KMH       = 5.0f;   // ignore impacts while parked

float CrashDetector::_angleDeg(const float a[3], const float b[3]) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    dot = fmaxf(-1.0f, fminf(1.0f, dot));
    return acosf(dot) * 180.0f / (float)M_PI;
}

bool CrashDetector::update(float ax, float ay, float az, float speedKmh, const CrashThresholds& t) {
    uint32_t now = millis();
    float dt = _lastMs ? (now - _lastMs) / 1000.0f : 0.01f;
    _lastMs = now;

    float mag = sqrtf(ax * ax + ay * ay + az * az);
    float g   = mag / G;

    // Speed in the last few seconds: follow rises immediately, decay ~10%/s.
    _recentMaxSpeed = fmaxf(speedKmh, _recentMaxSpeed * (1.0f - 0.1f * dt));

    // Gravity direction only from samples near 1 g (no hard braking/impacts).
    if (mag > 0.1f && g > 0.8f && g < 1.2f) {
        float u[3] = {ax / mag, ay / mag, az / mag};
        float kFast = fminf(1.0f, dt / 0.3f);   // ~0.3 s
        float kSlow = fminf(1.0f, dt / 3.0f);   // ~3 s
        for (int i = 0; i < 3; i++) {
            _cur[i] += kFast * (u[i] - _cur[i]);
            if (_state == State::IDLE) _ref[i] += kSlow * (u[i] - _ref[i]);
        }
        float nc = sqrtf(_cur[0] * _cur[0] + _cur[1] * _cur[1] + _cur[2] * _cur[2]);
        float nr = sqrtf(_ref[0] * _ref[0] + _ref[1] * _ref[1] + _ref[2] * _ref[2]);
        for (int i = 0; i < 3; i++) { _cur[i] /= nc; _ref[i] /= nr; }
        _refValid = true;
    }

    switch (_state) {
    case State::IDLE:
        if (_refValid && g > t.impactG && _recentMaxSpeed >= MIN_MOVING_KMH) {
            _state = State::CANDIDATE;
            _impactMs = now;
            _tiltSinceMs = 0;
            memcpy(_frozenRef, _ref, sizeof(_frozenRef));
            _info = {g, 0, _recentMaxSpeed, ""};
            Serial.printf("[I][CRASH] candidate: peak=%.1fg speed_before=%.0f km/h\n", g, _recentMaxSpeed);
        }
        return false;

    case State::CANDIDATE: {
        uint32_t since = now - _impactMs;
        if (g > _info.peakG) _info.peakG = g;

        float tilt = _angleDeg(_cur, _frozenRef);
        if (tilt > _info.tiltDeg) _info.tiltDeg = tilt;
        if (tilt > t.tiltDeg) {
            if (!_tiltSinceMs) _tiltSinceMs = now;
            if (now - _tiltSinceMs >= TILT_HOLD_MS) {
                _info.reason = "tilt";
                _state = State::IDLE;
                return true;
            }
        } else {
            _tiltSinceMs = 0;
        }

        if (since >= STOP_CHECK_AFTER_MS && _info.speedBeforeKmh >= t.minSpeedKmh && speedKmh < STOPPED_KMH) {
            _info.reason = "stop";
            _state = State::IDLE;
            return true;
        }

        if (since >= CANDIDATE_TIMEOUT_MS) {
            Serial.printf("[I][CRASH] candidate dismissed (tilt=%.0f°, speed=%.0f)\n", _info.tiltDeg, speedKmh);
            _state = State::IDLE;
        }
        return false;
    }
    }
    return false;
}
