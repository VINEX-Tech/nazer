// =============================================================
// WarningScreen.cpp: NAZER Phase 7
//
// 128×64 layout:
//
//  Y= 0  ┌────────────────────────────────┐
//         │       !! OVERSPEED !!          │  ← 10px bold, centred
//  Y=13  ├────────────────────────────────┤
//         │                                │
//  Y=16   │          95  km/h              │  ← 24px speed + unit
//         │                                │
//  Y=44  ├────────────────────────────────┤
//         │      +15 over 80 km/h          │  ← excess text
//  Y=63  └────────────────────────────────┘
//
// NOTE: flashing is handled entirely by ScreenManager (_warnFlashMs).
//       This function DOES NOT call invertDisplay(): that would
//       double-toggle and cancel out the flash effect.
// =============================================================

#include "WarningScreen.h"
#include "../Display.h"

void drawWarningScreen(float speedKmh, int speedLimit, float excessKmh) {
    Display.clear();

    // ── Header ────────────────────────────────────────────────
    Display.drawString(16, 0, "!! OVERSPEED !!");

    // ── Divider ───────────────────────────────────────────────
    Display.drawLine(0, 13, 127, 13);

    // ── Large speed number, centred ───────────────────────────
    int speed = (int)speedKmh;
    int numX;
    if (speed >= 100)     numX = 14;
    else if (speed >= 10) numX = 22;
    else                  numX = 34;

    Display.drawLargeNumber(numX, 16, speed);
    Display.drawString(68, 22, "km/h");

    // ── Divider ───────────────────────────────────────────────
    Display.drawLine(0, 44, 127, 44);

    // ── Excess line ───────────────────────────────────────────
    char buf[28];
    snprintf(buf, sizeof(buf), "+%.0f over %d km/h", excessKmh, speedLimit);
    // Centre the text (approx 10px per char, 10px font)
    int textX = (128 - (int)strlen(buf) * 6) / 2;
    if (textX < 0) textX = 0;
    Display.drawString(textX, 50, buf);

    Display.flush();
}
