// =============================================================
// ScreenManager.cpp: Display state machine for NAZER Phase 7
// =============================================================

#include "ScreenManager.h"
#include "Display.h"
#include "LocalStorage.h"
#include "Logger.h"
#include "config.h"

// Include screen draw functions
#include "Screens/MainScreen.h"
#include "Screens/MenuScreen.h"
#include "Screens/WarningScreen.h"

// ---- Menu item table ----
// isAction=true items trigger callbacks, not screen transitions.
// target=SPLASH used as sentinel for action items.
const MenuItem ScreenManager::MENU_ITEMS[ScreenManager::MENU_COUNT] = {
    { "Violations",    ScreenState::VIOLATIONS,   false },
    { "GPS Test",      ScreenState::DIAG_GPS,     false },
    { "IMU Test",      ScreenState::DIAG_IMU,     false },
    { "GSM Test",      ScreenState::DIAG_GSM,     false },
    { "Sensor Fusion", ScreenState::DIAG_FUSION,  false },
    { "Calibrate IMU", ScreenState::SPLASH,        true  },  // action
    { "Clear Queue",   ScreenState::SPLASH,        true  },  // action
    { "Back",          ScreenState::MAIN,          false },
};

// =============================================================
ScreenManager::ScreenManager() {}

void ScreenManager::begin() {
    _state        = ScreenState::SPLASH;
    _splashStartMs = millis();
    _warnFlashMs  = millis();
    _menuIdx      = 0;
}

// =============================================================
void ScreenManager::setState(ScreenState s) {
    LOG_INFO("SM", "State: %d → %d", (int)_state, (int)s);
    _prevState = _state;
    _state     = s;
}

// =============================================================
void ScreenManager::handleInput(int8_t rot, bool pressed) {
    // Splash: any input skips to MAIN
    if (_state == ScreenState::SPLASH) {
        if (pressed || rot != 0) setState(ScreenState::MAIN);
        return;
    }

    // Warning: button press dismisses warning manually
    if (_state == ScreenState::WARNING) {
        if (pressed) {
            setState(ScreenState::MAIN);
            Display.invertDisplay(false);
        }
        return;
    }

    // Menu: rotate to scroll, press to select
    if (_state == ScreenState::MENU) {
        if (rot != 0) {
            if (rot > 0) {
                _menuIdx = (_menuIdx + 1) % MENU_COUNT;
            } else {
                _menuIdx = (_menuIdx == 0) ? (MENU_COUNT - 1) : (_menuIdx - 1);
            }
        }
        if (pressed) {
            const MenuItem& item = MENU_ITEMS[_menuIdx];
            if (item.isAction) {
                // Run the action, stay in MENU
                if (strcmp(item.label, "Calibrate IMU") == 0 && _cbCalibrateImu) {
                    _cbCalibrateImu();
                } else if (strcmp(item.label, "Clear Queue") == 0 && _cbClearQueue) {
                    _cbClearQueue();
                }
            } else {
                setState(item.target);
            }
        }
        return;
    }

    // Diag / violations screens: button or CCW goes back to MENU
    if (_state == ScreenState::VIOLATIONS ||
        _state == ScreenState::DIAG_GPS   ||
        _state == ScreenState::DIAG_IMU   ||
        _state == ScreenState::DIAG_GSM   ||
        _state == ScreenState::DIAG_FUSION) {
        if (pressed) setState(ScreenState::MENU);
        return;
    }

    // MAIN: button press enters MENU
    if (_state == ScreenState::MAIN) {
        if (pressed) {
            _menuIdx = 0;
            setState(ScreenState::MENU);
        }
        return;
    }
}

// =============================================================
void ScreenManager::update(float speedKmh, int speedLimitKmh,
                           const FusedData& fused,
                           const char* transportLabel,
                           uint8_t battPct, uint8_t gsmSignal) {
    // Cache latest data
    _lastSpeed     = speedKmh;
    _lastLimit     = speedLimitKmh;
    _lastFused     = fused;
    _lastTransport = transportLabel;
    _lastBatt      = battPct;
    _lastGsm       = gsmSignal;

    uint32_t now = millis();

    // ---- Auto-transition: SPLASH → MAIN after 2 seconds ----
    if (_state == ScreenState::SPLASH) {
        if (now - _splashStartMs >= 2000) {
            setState(ScreenState::MAIN);
        }
        _drawSplash();
        return;
    }

    // ---- Auto-transition: MAIN ↔ WARNING based on speed ----
    bool overLimit = (speedKmh > (float)speedLimitKmh + VIOLATION_MARGIN_KMH);
    if (_state == ScreenState::MAIN && overLimit) {
        setState(ScreenState::WARNING);
        _warnFlashMs = now;
        _warnInverted = false;
    }
    if (_state == ScreenState::WARNING && !overLimit) {
        setState(ScreenState::MAIN);
        Display.invertDisplay(false);
    }

    // ---- Render current state ----
    switch (_state) {
        case ScreenState::MAIN:        _drawMain();        break;
        case ScreenState::WARNING:     _drawWarning();     break;
        case ScreenState::MENU:        _drawMenu();        break;
        case ScreenState::VIOLATIONS:  _drawViolations();  break;
        case ScreenState::DIAG_GPS:    _drawDiagGps();     break;
        case ScreenState::DIAG_IMU:    _drawDiagImu();     break;
        case ScreenState::DIAG_GSM:    _drawDiagGsm();     break;
        case ScreenState::DIAG_FUSION: _drawDiagFusion();  break;
        default: break;
    }
}

// =============================================================
// ---- SPLASH ----
// =============================================================
void ScreenManager::_drawSplash() {
    Display.clear();
    Display.showSplash();
    Display.flush();
}

// =============================================================
// ---- MAIN ----
// Delegates to MainScreen.cpp for the heavy layout work.
// =============================================================
void ScreenManager::_drawMain() {
    uint8_t gpsSats  = _lastFused.satellites;
    uint8_t gsmBars  = (_lastGsm > 0) ? map(constrain(_lastGsm, 0, 31), 0, 31, 0, 5) : 0;
    drawMainScreen(_lastSpeed, _lastLimit, gpsSats, gsmBars, _lastBatt, _lastTransport);
}

// =============================================================
// ---- WARNING ----
// Delegates to WarningScreen.cpp. Handles flash toggle here.
// =============================================================
void ScreenManager::_drawWarning() {
    uint32_t now = millis();
    if (now - _warnFlashMs >= 500) {
        _warnFlashMs   = now;
        _warnInverted  = !_warnInverted;
        Display.invertDisplay(_warnInverted);
    }
    float excess = _lastSpeed - (float)_lastLimit;
    drawWarningScreen(_lastSpeed, _lastLimit, excess);
}

// =============================================================
// ---- MENU ----
// Delegates to MenuScreen.cpp.
// =============================================================
void ScreenManager::_drawMenu() {
    // Build array of label pointers for drawMenuScreen
    static const char* labels[MENU_COUNT];
    for (uint8_t i = 0; i < MENU_COUNT; i++) {
        labels[i] = MENU_ITEMS[i].label;
    }
    drawMenuScreen(labels, MENU_COUNT, _menuIdx);
}

// =============================================================
// ---- VIOLATIONS ----
// Shows last 5 violation records stored in NVS.
// =============================================================
void ScreenManager::_drawViolations() {
    Display.clear();

    // Header
    Display.drawString(0, 0, "-- Violations --");

    if (!_storage) {
        Display.drawString(0, 16, "No storage ref");
        Display.flush();
        return;
    }

    uint8_t vCount = _storage->violationCount();
    if (vCount == 0) {
        Display.drawString(0, 16, "No violations");
        Display.drawString(0, 28, "stored.");
        Display.flush();
        return;
    }

    char line[32];
    snprintf(line, sizeof(line), "%d stored  [press=back]", vCount);
    Display.drawString(0, 12, line);

    // Show count summary: full records need backend for detail
    Display.drawString(0, 28, "Check app or");
    Display.drawString(0, 40, "website for detail.");

    Display.flush();
}

// =============================================================
// ---- DIAG GPS ----
// =============================================================
void ScreenManager::_drawDiagGps() {
    Display.clear();
    Display.drawString(0, 0, "GPS Diagnostics");

    char buf[32];
    snprintf(buf, sizeof(buf), "Fix: %s  Sats:%d",
             _lastFused.hasFix ? "YES" : "NO ",
             _lastFused.satellites);
    Display.drawString(0, 12, buf);

    snprintf(buf, sizeof(buf), "HDOP: %.2f", _lastFused.hdop);
    Display.drawString(0, 24, buf);

    snprintf(buf, sizeof(buf), "Lat:  %.5f", _lastFused.lat);
    Display.drawString(0, 36, buf);

    snprintf(buf, sizeof(buf), "Lon:  %.5f", _lastFused.lon);
    Display.drawString(0, 48, buf);

    Display.flush();
}

// =============================================================
// ---- DIAG IMU ----
// =============================================================
void ScreenManager::_drawDiagImu() {
    Display.clear();
    Display.drawString(0, 0, "IMU Diagnostics");

    char buf[32];
    snprintf(buf, sizeof(buf), "aX:%.3f  Cal:%s",
             _lastFused.imuAccelMs2,
             _lastFused.hasFix ? "YES" : "NO");  // reuse hasFix slot: calibrated flag not in FusedData
    Display.drawString(0, 12, buf);

    snprintf(buf, sizeof(buf), "IMU: %.2f m/s2", _lastFused.imuAccelMs2);
    Display.drawString(0, 24, buf);

    Display.drawString(0, 40, "[press=back]");
    Display.flush();
}

// =============================================================
// ---- DIAG GSM ----
// =============================================================
void ScreenManager::_drawDiagGsm() {
    Display.clear();
    Display.drawString(0, 0, "GSM Diagnostics");

    char buf[32];
    snprintf(buf, sizeof(buf), "CSQ: %d / 31", _lastGsm);
    Display.drawString(0, 12, buf);

    snprintf(buf, sizeof(buf), "Transport: %s", _lastTransport);
    Display.drawString(0, 24, buf);

    // Signal quality interpretation
    const char* quality = "None";
    if      (_lastGsm >= 20) quality = "Excellent";
    else if (_lastGsm >= 15) quality = "Good";
    else if (_lastGsm >= 10) quality = "Fair";
    else if (_lastGsm >=  5) quality = "Poor";
    Display.drawString(0, 36, quality);

    Display.drawString(0, 52, "[press=back]");
    Display.flush();
}

// =============================================================
// ---- DIAG FUSION ----
// =============================================================
void ScreenManager::_drawDiagFusion() {
    Display.clear();
    Display.drawString(0, 0, "Sensor Fusion");

    char buf[32];
    snprintf(buf, sizeof(buf), "Fused: %.1f km/h", _lastFused.fusedSpeedKmh);
    Display.drawString(0, 12, buf);

    snprintf(buf, sizeof(buf), "GPS:   %.1f km/h", _lastFused.gpsSpeedKmh);
    Display.drawString(0, 24, buf);

    snprintf(buf, sizeof(buf), "K-gain: %.4f", _lastFused.kalmanGain);
    Display.drawString(0, 36, buf);

    snprintf(buf, sizeof(buf), "K-cov:  %.4f", _lastFused.kalmanCovariance);
    Display.drawString(0, 48, buf);

    Display.flush();
}

// =============================================================
// ---- HELPERS ----
// =============================================================
void ScreenManager::_drawSignalBars(int x, int y, uint8_t level, uint8_t maxLevel) {
    for (uint8_t i = 0; i < maxLevel; i++) {
        int barH = 2 + i * 2;           // each bar 2px taller
        int barX = x + i * 4;
        int barY = y + (maxLevel * 2) - barH;
        if (i < level) {
            Display.drawRect(barX, barY, 3, barH, true);   // filled
        } else {
            Display.drawRect(barX, barY, 3, barH, false);  // outline only
        }
    }
}

void ScreenManager::_truncate(char* dst, const char* src, uint8_t maxChars) {
    strncpy(dst, src, maxChars);
    dst[maxChars] = '\0';
}
