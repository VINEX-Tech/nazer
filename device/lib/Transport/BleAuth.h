#pragma once
// =============================================================
// BleAuth.h: pairing-code gate for BLE commands.
//
// A phone that connects must first write
//     {"cmd":"auth","code":"<BLE_PAIR_CODE>"}
// Until that succeeds, every other command is refused and the
// device sends no telemetry, events or configuration over BLE.
// The session ends when the phone disconnects (BLETransport
// clears the flag in its connect and disconnect callbacks).
//
// Brute-force guard: after MAX_FAILURES wrong codes, all auth
// attempts are refused for LOCKOUT_MS. The counter survives
// reconnects, so dropping the link does not reset it.
//
// The code is compared in constant time. It still crosses the
// air in clear text: see SECURITY.md for what this does and does
// not protect against.
//
// Pure C++11 with no Arduino dependency so it runs in the host
// checks (test/host/test_ble_auth.cpp).
// =============================================================
#include <stddef.h>
#include <stdint.h>
#include <string.h>

class BleAuth {
public:
    enum Result { OK, WRONG, LOCKED, NOT_CONFIGURED };

    static const uint8_t  MAX_FAILURES = 5;
    static const uint32_t LOCKOUT_MS   = 60000;
    static const size_t   MIN_CODE_LEN = 8;
    static const size_t   MAX_CODE_LEN = 64;

    // `code` must outlive this object (a string literal from config.h).
    explicit BleAuth(const char* code) : _code(code) {}

    // False for a missing, short or placeholder code. With no usable code
    // configured the device refuses every command: it fails closed.
    bool configured() const {
        if (!_code) return false;
        size_t n = strlen(_code);
        if (n < MIN_CODE_LEN || n > MAX_CODE_LEN) return false;
        return strcmp(_code, placeholder()) != 0;
    }

    Result check(const char* given, uint32_t nowMs) {
        if (!configured()) return NOT_CONFIGURED;
        if (_locked) {
            if ((uint32_t)(nowMs - _lockedAtMs) < LOCKOUT_MS) return LOCKED;
            _locked = false;      // lockout over, wrap-safe via unsigned subtraction
            _failures = 0;
        }
        if (given && equal(given, _code)) {
            _failures = 0;
            return OK;
        }
        if (++_failures >= MAX_FAILURES) {
            _locked = true;
            _lockedAtMs = nowMs;
        }
        return WRONG;
    }

    // Only "auth" is allowed before the session is authorised.
    static bool allowed(const char* cmd, bool authorized) {
        if (authorized) return true;
        return cmd && strcmp(cmd, "auth") == 0;
    }

    static const char* reason(Result r) {
        switch (r) {
            case OK:             return "ok";
            case WRONG:          return "wrong_code";
            case LOCKED:         return "locked";
            case NOT_CONFIGURED: return "not_configured";
        }
        return "unknown";
    }

    uint8_t failures() const { return _failures; }

    // The value shipped in config.example.h. A device still using it is unpaired.
    static const char* placeholder() { return "your_ble_pairing_code_here"; }

private:
    // Compares every byte of the longer string so the time does not depend
    // on where the first mismatch is.
    static bool equal(const char* a, const char* b) {
        size_t la = strlen(a), lb = strlen(b);
        size_t n = la > lb ? la : lb;
        uint8_t diff = (uint8_t)(la != lb);
        for (size_t i = 0; i < n; i++) {
            uint8_t ca = i < la ? (uint8_t)a[i] : 0;
            uint8_t cb = i < lb ? (uint8_t)b[i] : 0;
            diff |= (uint8_t)(ca ^ cb);
        }
        return diff == 0;
    }

    const char* _code;
    uint8_t  _failures   = 0;
    bool     _locked     = false;
    uint32_t _lockedAtMs = 0;
};
