// =============================================================
// SplashScreen.cpp: NAZER Phase 7
//
// 128×64 layout:
//
//  Y= 0  ┌────────────────────────────────┐
//         │           NAZER               │  ← 24px font, centred
//  Y=28  ├────────────────────────────────┤
//         │      Speed Monitor            │  ← 10px font, centred
//         │           v2.0                │
//  Y=52  ├────────────────────────────────┤
//         │       Initializing...         │  ← 10px, centred
//  Y=63  └────────────────────────────────┘
//
// NOTE: Display.showSplash() already renders a correct splash using
// the _oled object directly with TEXT_ALIGN_CENTER and ArialMT_Plain_24.
// This function just delegates to it: no duplication.
// =============================================================

#include "SplashScreen.h"
#include "Display.h"

void drawSplashScreen() {
    // Display.showSplash() uses ArialMT_Plain_24 centered at y=10
    // for "NAZER", then 10px for the subtitle: already correct dimensions.
    // We just call it directly rather than re-implementing.
    Display.showSplash();
}
