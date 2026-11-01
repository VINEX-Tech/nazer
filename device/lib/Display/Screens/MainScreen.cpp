// =============================================================
// MainScreen.cpp: NAZER Phase 7
//
// 128×64 px OLED layout:
//
//  Y= 0  ┌────────────────────────────────┐
//         │ G07   bars     BLE      batt   │
//  Y=13  ├────────────────────────────────┤
//         │        72       LIM 60         │
//         │      km/h       SAFE           │
//  Y=43  ├────────────────────────────────┤
//         │ progress to current limit       │
//  Y=63  └────────────────────────────────┘
// =============================================================

#include "MainScreen.h"
#include "../Display.h"
#include <string.h>

// Draw 5 signal bars (GSM / GPS quality indicator) at x,y
// level: 0–5 filled bars
static void _drawBars(int x, int y, uint8_t level) {
    for (uint8_t i = 0; i < 5; i++) {
        int barH = 3 + i * 2;          // bars: 3,5,7,9,11 px tall
        int barX = x + i * 5;
        int barY = y + 11 - barH;      // all bars bottom-aligned at y+11
        if (i < level) {
            Display.drawRect(barX, barY, 4, barH, true);   // filled
        } else {
            Display.drawRect(barX, barY, 4, barH, false);  // outline only
        }
    }
}

static void _drawBattery(int x, int y, uint8_t pct) {
    Display.drawRect(x, y + 2, 18, 8, false);
    Display.drawRect(x + 18, y + 4, 2, 4, true);
    int fillW = (int)(constrain(pct, 0, 100) * 16 / 100);
    if (fillW > 0) {
        Display.drawRect(x + 1, y + 3, fillW, 6, true);
    }
}

static const char* _shortTransport(const char* label) {
    if (!label || label[0] == '\0') return "OFF";
    if (strncmp(label, "WiFi", 4) == 0) return "WIFI";
    if (strncmp(label, "BLE", 3) == 0) return "BLE";
    if (strncmp(label, "GSM", 3) == 0) return "GSM";
    return "OFF";
}

void drawMainScreen(float speedKmh, int speedLimit,
                    uint8_t gpsSats, uint8_t gsmSignal, uint8_t battPct,
                    const char* transportLabel) {
    Display.clear();

    // ── Row 0: status bar ──────────────────────────────────────
    // GPS satellites count
    char satBuf[8];
    snprintf(satBuf, sizeof(satBuf), "G%02d", gpsSats);
    Display.drawString(0, 0, satBuf);

    _drawBars(28, 0, constrain(gsmSignal, 0, 5));

    Display.drawString(66, 0, _shortTransport(transportLabel));
    _drawBattery(106, 0, battPct);

    // ── Divider ────────────────────────────────────────────────
    Display.drawLine(0, 13, 127, 13);

    // ── Speed: large centred number ──────────────────────────
    // ArialMT_Plain_24 is 24px tall.
    // Place at Y=16 → bottom edge at Y=40, well within the 64px screen.
    int speed = (int)speedKmh;

    // Centre the number: 3-digit numbers are ~42px wide, 2-digit ~28px
    int numX;
    if (speed >= 100)      numX = 4;
    else if (speed >= 10)  numX = 14;
    else                   numX = 26;

    Display.drawLargeNumber(numX, 16, speed);

    Display.drawString(18, 39, "km/h");

    char limBuf[12];
    snprintf(limBuf, sizeof(limBuf), "LIM %d", speedLimit);
    Display.drawString(78, 18, limBuf);

    const bool overLimit = speedKmh > (float)speedLimit;
    Display.drawString(82, 32, overLimit ? "OVER" : "SAFE");

    // Visual speed-bar under the number (progress toward speed limit)
    if (speedLimit > 0) {
        int pct     = (int)((speedKmh / (float)speedLimit) * 100);
        pct         = constrain(pct, 0, 120);
        int barW    = (int)(118 * min(pct, 100) / 100);
        Display.drawRect(4, 48, 120, 8, false);
        if (barW > 0)
            Display.drawRect(5, 49, barW, 6, true);
        if (pct > 100) {
            Display.drawLine(122, 48, 127, 55);
            Display.drawLine(127, 48, 122, 55);
        }
    }

    Display.drawString(0, 57, gpsSats >= 3 ? "GPS FIX" : "NO GPS");
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", battPct);
    Display.drawString(104, 57, battBuf);

    Display.flush();
}
