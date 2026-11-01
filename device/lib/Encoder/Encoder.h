#pragma once
// ============================================================
// Encoder.h: EC11 Rotary Encoder with ISR-based counting
//
// Hardware notes for NAZER:
//   ENC_A_PIN (GPIO35): TRA: input-only, NO internal pull-up!
//   ENC_B_PIN (GPIO34): TRB: input-only, NO internal pull-up!
//   ENC_BTN_PIN (GPIO32): PSH: has INPUT_PULLUP available
//
//   You MUST have 10kΩ external pull-up resistors on GPIO34/35.
//   Without them these pins float and generate phantom counts.
//
// How it works:
//   The EC11 generates 2 pulses per physical detent.
//   We read both edges of channel A and sample channel B to
//   determine direction. Every 2 raw pulses = 1 reported step.
// ============================================================

#include <Arduino.h>

class Encoder {
public:
    // pinA = TRA (GPIO35), pinB = TRB (GPIO34), pinBtn = PSH (GPIO32)
    Encoder(uint8_t pinA, uint8_t pinB, uint8_t pinBtn);

    // Call once in setup()
    void begin();

    // Returns +1 (CW), -1 (CCW), or 0 (no movement) since last call.
    // Consume this every loop() iteration.
    int8_t getStep();

    // Returns true once per button press (edge-triggered, debounced).
    // Must call repeatedly in loop(): clears itself after returning true.
    bool wasPressed();

    // Raw pulse count: useful for debugging the ISR
    int32_t getRawCount() const { return _rawCount; }

private:
    uint8_t _pinA, _pinB, _pinBtn;

    // ISR variables: must be volatile because they change in interrupt context
    volatile int32_t _rawCount;      // raw pulse accumulator
    volatile uint8_t _lastA;         // previous state of channel A

    // State tracked in getStep() for per-detent reporting
    int32_t _lastReportedCount;      // rawCount value at last getStep() call

    // Button debounce state
    uint32_t _lastBtnTime;           // millis() of last button change
    bool     _lastBtnState;          // debounced button state
    bool     _pressedFlag;           // set by loop, cleared after read

    static const uint16_t DEBOUNCE_MS = 50;  // 50ms debounce for the button
    static const int8_t   PULSES_PER_DETENT = 2; // EC11 = 2 pulses per click

    // ISR handler: must be static to be registered with attachInterrupt()
    // We need one per instance, so use a static pointer trick.
    static Encoder* _instance;
    static void IRAM_ATTR _isr();   // IRAM_ATTR = runs from fast RAM, safe during flash ops
};