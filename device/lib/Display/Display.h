#pragma once
// ============================================================
// Display.h: SH1106 OLED abstraction for NAZER
//
// Singleton: use the global `g_display` instance everywhere.
//   #include "Display.h"
//   g_display.begin();
//   g_display.showSplash();
//
// Screen files (MainScreen.cpp etc.) call g_display.method().
// ============================================================

#include <Arduino.h>
#include <SH1106Wire.h>

class DisplayDriver {
public:
    DisplayDriver(uint8_t addr, uint8_t sda, uint8_t scl);

    // ---- Lifecycle ----
    bool begin();

    // ---- HIGH-LEVEL screens ----
    void showCounter(int32_t counter, uint32_t presses);
    void showSplash();
    void showError(const char* msg);
    void showMessage(const char* line1, const char* line2 = nullptr);

    // ---- LOW-LEVEL primitives (used by Screens/*.cpp) ----
    void clear();                                       // clear buffer (no flush)
    void flush();                                       // push buffer to hardware
    void drawString(int x, int y, const char* text);
    void drawLargeNumber(int x, int y, int value);
    void drawLine(int x0, int y0, int x1, int y1);
    void drawRect(int x, int y, int w, int h, bool filled = false);
    void invertDisplay(bool invert);                    // toggle invert state

private:
    SH1106Wire _oled;
    bool _inverted = false;
    void _drawBar(int x, int y, int w, int h, int pct);
};

// ---- Global singleton ----
// Defined in Display.cpp. Include this header and use g_display directly.
extern DisplayDriver g_display;

// ---- Compatibility alias ----
// The original Screens/*.cpp files call  Display.clear() etc.
// This macro maps "Display" → "g_display" so those files need no changes.
#define Display g_display