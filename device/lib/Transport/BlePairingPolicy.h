#pragma once
// =============================================================
// BlePairingPolicy.h: passkey rules and the bonding window.
//
// Link security (BLETransport.cpp) uses LE Secure Connections with
// MITM protection. The passkey comes from one of two places:
//
//   Display fitted (NAZER_HAS_DISPLAY 1): a fresh random passkey
//     for every pairing attempt, shown on the OLED and on serial.
//   No display (the current prototype): the static BLE_PASSKEY
//     from config, a documented fallback.
//
// Either way it fails closed: a static passkey of 0 or outside
// 1..999999 means BLE security and advertising do not start.
//
// New bonds are accepted only while the pairing window is open:
// WINDOW_MS after boot, or after a long press of the encoder
// button where one is fitted. Phones that bonded earlier keep
// reconnecting outside the window. The app-level pairing code
// (BleAuth.h) is still required on top of the bond.
//
// Pure C++11, no Arduino dependency: host-checked in
// test/host/test_ble_pairing_policy.cpp.
// =============================================================
#include <stdint.h>

namespace blepair {

static const uint32_t MAX_PASSKEY = 999999;
static const uint32_t WINDOW_MS   = 120000;   // bonding allowed this long after boot or a long press
static const uint32_t LONG_PRESS_MS = 3000;   // button hold that reopens the window

// A static passkey is usable when it is 1..999999. 0 is the shipped placeholder.
inline bool validStaticPasskey(long long passkey) {
    return passkey >= 1 && passkey <= (long long)MAX_PASSKEY;
}

// Maps 32 random bits to a passkey in 1..999999 (never the placeholder 0).
inline uint32_t passkeyFromRandom(uint32_t random) {
    return random % MAX_PASSKEY + 1;
}

class Window {
public:
    void open(uint32_t nowMs) { _openedAt = nowMs; _isOpen = true; }
    void close() { _isOpen = false; }

    // Wrap-safe across the 32-bit millis() rollover.
    bool isOpen(uint32_t nowMs) {
        if (_isOpen && (uint32_t)(nowMs - _openedAt) >= WINDOW_MS) _isOpen = false;
        return _isOpen;
    }

    uint32_t remainingMs(uint32_t nowMs) {
        return isOpen(nowMs) ? WINDOW_MS - (uint32_t)(nowMs - _openedAt) : 0;
    }

private:
    uint32_t _openedAt = 0;
    bool     _isOpen   = false;
};

// Detects a button held for LONG_PRESS_MS. Feed it the raw level every loop.
class LongPress {
public:
    // Returns true once per hold, when the hold reaches LONG_PRESS_MS.
    bool update(bool pressed, uint32_t nowMs) {
        if (!pressed) { _down = false; _fired = false; return false; }
        if (!_down) { _down = true; _since = nowMs; return false; }
        if (!_fired && (uint32_t)(nowMs - _since) >= LONG_PRESS_MS) { _fired = true; return true; }
        return false;
    }

private:
    bool     _down = false, _fired = false;
    uint32_t _since = 0;
};

}  // namespace blepair
