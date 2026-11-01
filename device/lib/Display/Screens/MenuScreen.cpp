// =============================================================
// MenuScreen.cpp: NAZER Phase 7
//
// 128×64 layout: 5 visible rows at 12px each:
//
//  Y= 0  ┌────────────────────────────────┐
//         │  MENU                          │  ← header, 10px font
//  Y=12  ├────────────────────────────────┤
//         │ > Violations               [5] │  ← selected: arrow + bracket
//         │   GPS Test                     │
//         │   IMU Test                     │
//         │   GSM Test                     │
//         │   Sensor Fusion                │
//  Y=63  └────────────────────────────────┘
//
// The ThingPulse SH1106 lib has no setColor(BLACK) in all versions,
// so we avoid filled-rect highlights (they paint over text in white
// making it disappear). Instead we use a ">" prefix + underline for
// the selected row: always readable regardless of invert state.
// =============================================================

#include "MenuScreen.h"
#include "../Display.h"
#include <string.h>

void drawMenuScreen(const char** items, uint8_t count, uint8_t selectedIdx) {
    Display.clear();

    // ── Header ────────────────────────────────────────────────
    char title[24];
    snprintf(title, sizeof(title), "MENU %d/%d", selectedIdx + 1, count);
    Display.drawString(0, 0, title);
    Display.drawLine(0, 12, 127, 12);

    // ── Rows ──────────────────────────────────────────────────
    const int LINE_H  = 12;   // 10px font + 2px gap
    const int VISIBLE = 4;    // rows 13..61 = 48px / 12px = 4 rows
    const int Y_START = 14;

    // Scroll window: keep selectedIdx visible
    int startIdx = 0;
    if ((int)selectedIdx >= startIdx + VISIBLE) {
        startIdx = (int)selectedIdx - VISIBLE + 1;
    }

    for (int i = 0; i < VISIBLE && (startIdx + i) < (int)count; i++) {
        int row = startIdx + i;
        int y   = Y_START + i * LINE_H;
        bool selected = (row == (int)selectedIdx);

        const char* icon = ">";
        if (strstr(items[row], "Violations")) icon = "!";
        else if (strstr(items[row], "GPS")) icon = "G";
        else if (strstr(items[row], "IMU")) icon = "I";
        else if (strstr(items[row], "GSM")) icon = "S";
        else if (strstr(items[row], "Fusion")) icon = "F";
        else if (strstr(items[row], "Calibrate")) icon = "*";
        else if (strstr(items[row], "Clear")) icon = "X";
        else if (strstr(items[row], "Back")) icon = "<";

        Display.drawString(0, y, selected ? ">" : " ");
        Display.drawString(12, y, icon);
        Display.drawString(24, y, items[row]);

        if (selected) {
            Display.drawLine(0, y + 10, 116, y + 10);
        }

        // Scroll indicator: show "v" or "^" at far right if list is scrollable
        if (i == 0 && startIdx > 0) {
            Display.drawString(120, y, "^");
        }
        if (i == VISIBLE - 1 && (startIdx + VISIBLE) < (int)count) {
            Display.drawString(120, y, "v");
        }
    }

    // ── Bottom hint ───────────────────────────────────────────
    // Only when we have room (when fewer than VISIBLE items shown)
    if (count <= VISIBLE) {
        Display.drawString(0, 62, "turn=scroll  press=select");
    }

    Display.flush();
}
