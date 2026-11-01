// ============================================================
// test_display_encoder.cpp: NAZER Phase 1 Test
//
// PURPOSE:
//   Verify that the SH1106 OLED and EC11 encoder are wired
//   correctly and behave as expected before moving to Phase 2.
//
// HOW TO USE:
//   In platformio.ini, set:
//     build_src_filter = -<*> +<../test/test_display_encoder.cpp>
//   Flash to board, open Serial Monitor at 115200 baud.
//
// PASS CRITERIA (check each box manually):
//   [ ] OLED lights up and shows splash screen on boot
//   [ ] Rotating CW increases the counter by +1 per click
//   [ ] Rotating CCW decreases the counter by -1 per click
//   [ ] Each physical detent = exactly ±1 (no skipping, no double-counting)
//   [ ] Button press count increments once per click (no bounce)
//   [ ] Serial Monitor shows every event with timestamp
//   [ ] After 100+ encoder steps, counter matches expected total
//
// TROUBLESHOOTING:
//   CW gives -1 instead of +1 →  swap ENC_A_PIN and ENC_B_PIN in config.h
//   Each click = 2 steps      →  change PULSES_PER_DETENT in Encoder.h to 4
//   Phantom steps when idle   →  external pull-up resistors missing on GPIO34/35
//   OLED doesn't light up     →  run I2C scanner, confirm 0x3C is present
//   Screen is upside down     →  change flipScreenVertically() in Display.cpp
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "../lib/Logger/Logger.h"
#include "../lib/Display/Display.h"
#include "../lib/Encoder/Encoder.h"

// ---- Module instances ----
// Display singleton is g_display, defined in Display.cpp
Encoder encoder(ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN);

// ---- State ----
int32_t  counter = 0;     // encoder position, can go negative
uint32_t presses = 0;     // button press count

// ---- Timing ----
uint32_t lastDisplayUpdate = 0;   // when we last refreshed the OLED
uint32_t lastHeartbeat     = 0;   // when we last blinked the LED
uint32_t testStartTime     = 0;

// ---- Test pass/fail tracking ----
bool cwSeen  = false;   // have we seen at least one CW step?
bool ccwSeen = false;   // have we seen at least one CCW step?
bool btnSeen = false;   // have we seen at least one button press?

void printPassReport();

// ============================================================
void setup() {
    // Logger must come first: it starts Serial
    Logger::begin(115200);
    LOG_INFO("TEST", "=== Phase 1: Display + Encoder Test ===");
    LOG_INFO("TEST", "Pinout: ENC_A=%d ENC_B=%d BTN=%d SDA=%d SCL=%d OLED=0x%02X",
             ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN, SDA_PIN, SCL_PIN, OLED_I2C_ADDR);

    // Status LED: blinks to show the MCU is alive even if g_display fails
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // ---- Display init ----
    LOG_INFO("DISP", "Initializing SH1106 OLED...");
    if (g_display.begin()) {
        LOG_INFO("DISP", "OLED OK");
    } else {
        LOG_ERROR("DISP", "OLED init failed! Check I2C wiring and run I2C scanner.");
        // Don't halt: encoder can still be tested via Serial alone
    }

    // Show splash for 2 seconds so user can confirm the screen works
    g_display.showSplash();
    delay(2000);

    // ---- Encoder init ----
    LOG_INFO("ENC", "Initializing encoder on A=%d B=%d BTN=%d", ENC_A_PIN, ENC_B_PIN, ENC_BTN_PIN);
    encoder.begin();
    LOG_INFO("ENC", "Encoder OK: rotate and press to test");

    // Ready
    testStartTime = millis();
    LOG_INFO("TEST", "Ready. Rotate encoder and press button.");
    LOG_INFO("TEST", "Rotate CW  → counter should increase (+1 per click)");
    LOG_INFO("TEST", "Rotate CCW → counter should decrease (-1 per click)");
    LOG_INFO("TEST", "Press BTN  → press count increments by 1");
}

// ============================================================
void loop() {
    uint32_t now = millis();

    // ---- 1. Read encoder step ----
    // getStep() returns +1, -1, or 0: call every loop iteration
    int8_t step = encoder.getStep();
    if (step != 0) {
        counter += step;

        // Log every movement with direction and running total
        LOG_INFO("ENC", "Step: %+d  →  Counter: %ld  (raw pulses: %ld)",
                 step, counter, encoder.getRawCount());

        // Track that both directions have been seen for pass report
        if (step > 0) cwSeen  = true;
        if (step < 0) ccwSeen = true;
    }

    // ---- 2. Read button ----
    // wasPressed() is edge-triggered and debounced: safe to call every loop
    if (encoder.wasPressed()) {
        presses++;
        btnSeen = true;
        LOG_INFO("BTN", "Button pressed! Total presses: %lu  Counter at press: %ld",
                 presses, counter);
    }

    // ---- 3. Update g_display at DISPLAY_REFRESH_MS rate (not every loop) ----
    // Updating every loop would cause flickering due to SH1106's I2C redraw time
    if ((now - lastDisplayUpdate) >= DISPLAY_REFRESH_MS) {
        lastDisplayUpdate = now;
        g_display.showCounter(counter, presses);
    }

    // ---- 4. Heartbeat LED: blink every 500ms so we can see the MCU is alive ----
    if ((now - lastHeartbeat) >= 500) {
        lastHeartbeat = now;
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    }

    // ---- 5. Auto-report after 30 seconds of inactivity (no encoder events) ----
    // This helps you know when testing is "done enough" to move to Phase 2
    static uint32_t lastActivity = 0;
    if (step != 0 || encoder.wasPressed()) {  // wasPressed() already consumed above, this is always false: harmless
        lastActivity = now;
    }
    if (step != 0) lastActivity = now;

    // Print a summary report every 60 seconds
    static uint32_t lastReport = 0;
    if ((now - lastReport) >= 60000) {
        lastReport = now;
        printPassReport();
    }
}

// ============================================================
// Print a human-readable summary of pass/fail status
// ============================================================
void printPassReport() {
    LOG_INFO("TEST", "--- Phase 1 Interim Report ---");
    LOG_INFO("TEST", "  CW rotation seen:   %s", cwSeen  ? "YES ✓" : "NO (rotate CW)");
    LOG_INFO("TEST", "  CCW rotation seen:  %s", ccwSeen ? "YES ✓" : "NO (rotate CCW)");
    LOG_INFO("TEST", "  Button press seen:  %s", btnSeen ? "YES ✓" : "NO (press button)");
    LOG_INFO("TEST", "  Counter value now:  %ld", counter);
    LOG_INFO("TEST", "  Total presses:      %lu", presses);
    LOG_INFO("TEST", "  Raw encoder count:  %ld", encoder.getRawCount());
    LOG_INFO("TEST", "  Uptime:             %lu ms", millis() - testStartTime);

    if (cwSeen && ccwSeen && btnSeen) {
        LOG_INFO("TEST", "  STATUS: PASS: all interactions confirmed ✓");
        LOG_INFO("TEST", "  Ready to move to Phase 2 (GPS).");
    } else {
        LOG_WARN("TEST", "  STATUS: INCOMPLETE: interact more to confirm all paths.");
    }
    LOG_INFO("TEST", "------------------------------");
}
