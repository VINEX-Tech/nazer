#pragma once
// =============================================================
// ScreenManager.h: Display state machine for NAZER Phase 7
//
// States:
//   SPLASH      → shown 2s on boot, auto-transitions to MAIN
//   MAIN        → default running screen
//   WARNING     → shown when speed > limit + margin, auto-clears
//   MENU        → entered by encoder press, exited by Back item
//   VIOLATIONS  → shows last 5 NVS violation records
//   DIAG_GPS    → live GPS diagnostics
//   DIAG_IMU    → live IMU diagnostics
//   DIAG_GSM    → GSM signal + GPRS status
//   DIAG_FUSION → Kalman filter internals
//
// Usage:
//   ScreenManager sm;
//   sm.begin();
//   // in loop():
//   sm.handleInput(encoder.getStep(), encoder.wasPressed());
//   sm.update(speed, limit, fusedData, transportLabel);
// =============================================================

#include <Arduino.h>
#include "SensorFusion.h"   // FusedData

// Forward declarations: keep includes out of this header
class LocalStorage;

enum class ScreenState {
    SPLASH,
    MAIN,
    WARNING,
    MENU,
    VIOLATIONS,
    DIAG_GPS,
    DIAG_IMU,
    DIAG_GSM,
    DIAG_FUSION
};

// Menu item definitions
struct MenuItem {
    const char* label;
    ScreenState target;       // SPLASH = "action item" (no screen transition)
    bool        isAction;     // true = triggers a callback, not a screen
};

class ScreenManager {
public:
    ScreenManager();

    // Call once in setup(): after Display.begin()
    void begin();

    // Handle encoder input: call every loop()
    // rot: +1 CW, -1 CCW, 0 none   pressed: true on button press edge
    void handleInput(int8_t rot, bool pressed);

    // Render current screen: call every loop()
    // transport: "WiFi" | "BLE" | "GSM" | "OFFLINE" from TransportManager.statusLabel()
    void update(float speedKmh, int speedLimitKmh,
                const FusedData& fused,
                const char* transportLabel,
                uint8_t battPct     = 0,
                uint8_t gsmSignal   = 0);

    // Force a state change (e.g. from main.cpp on boot)
    void setState(ScreenState s);

    ScreenState state() const { return _state; }

    // Inject dependencies: call before begin() if diag screens need them
    void setStorage(LocalStorage* ls) { _storage = ls; }

    // ---- Action callbacks: set by main.cpp ----
    void setOnCalibrateImu(void (*cb)())   { _cbCalibrateImu = cb; }
    void setOnClearQueue(void (*cb)())     { _cbClearQueue   = cb; }

private:
    ScreenState _state        = ScreenState::SPLASH;
    ScreenState _prevState    = ScreenState::MAIN;

    // Splash timer
    uint32_t    _splashStartMs = 0;

    // Warning flash state
    uint32_t    _warnFlashMs   = 0;
    bool        _warnInverted  = false;

    // Menu state
    uint8_t     _menuIdx       = 0;
    static const uint8_t MENU_COUNT = 8;
    static const MenuItem MENU_ITEMS[MENU_COUNT];

    // Dependencies
    LocalStorage* _storage     = nullptr;

    // Action callbacks
    void (*_cbCalibrateImu)()  = nullptr;
    void (*_cbClearQueue)()    = nullptr;

    // Last known data (for screens that show snapshots)
    FusedData   _lastFused     = {};
    float       _lastSpeed     = 0.0f;
    int         _lastLimit     = 60;
    uint8_t     _lastBatt      = 0;
    uint8_t     _lastGsm       = 0;
    const char* _lastTransport = "OFFLINE";

    // ---- Internal draw methods ----
    void _drawSplash();
    void _drawMain();
    void _drawWarning();
    void _drawMenu();
    void _drawViolations();
    void _drawDiagGps();
    void _drawDiagImu();
    void _drawDiagGsm();
    void _drawDiagFusion();

    // Helper: draw GSM signal bars (0–5) at position x,y
    void _drawSignalBars(int x, int y, uint8_t level, uint8_t maxLevel = 5);
    // Helper: truncate string to fit OLED width
    void _truncate(char* dst, const char* src, uint8_t maxChars);
};
