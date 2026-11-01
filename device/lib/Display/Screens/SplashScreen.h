#pragma once
// =============================================================
// SplashScreen.h: Boot splash screen for NAZER Phase 7
//
// Called by Display.showSplash() (already declared in Display.h).
// This header exists so ScreenManager can call it directly too.
// =============================================================
#include <Arduino.h>

// Draws the NAZER boot splash on the OLED.
// Call once: does NOT loop or delay.
void drawSplashScreen();
