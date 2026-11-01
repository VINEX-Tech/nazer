// ============================================================
// Encoder.cpp: EC11 ISR-based implementation
// ============================================================

#include "Encoder.h"

// Static instance pointer for the ISR (only supports one encoder at a time)
Encoder* Encoder::_instance = nullptr;

Encoder::Encoder(uint8_t pinA, uint8_t pinB, uint8_t pinBtn)
    : _pinA(pinA), _pinB(pinB), _pinBtn(pinBtn),
      _rawCount(0), _lastA(0),
      _lastReportedCount(0),
      _lastBtnTime(0), _lastBtnState(true), _pressedFlag(false)
{}

void Encoder::begin() {
    // GPIO34 and GPIO35 are input-only: cannot use INPUT_PULLUP.
    // External 10kΩ pull-ups must be on the PCB!
    pinMode(_pinA, INPUT);
    pinMode(_pinB, INPUT);

    // GPIO32 supports INPUT_PULLUP
    pinMode(_pinBtn, INPUT_PULLUP);

    // Capture initial state so we don't generate a phantom step at boot
    _lastA = digitalRead(_pinA);

    // Register the ISR: CHANGE triggers on both rising and falling edges
    // This gives us 2 interrupts per pulse = 4 per detent, which we'll
    // divide back down in getStep()
    _instance = this;
    attachInterrupt(digitalPinToInterrupt(_pinA), _isr, CHANGE);
}

// IRAM_ATTR: this ISR runs from internal RAM so it's safe even if flash
// is busy (e.g. during NVS write). Never call Serial or delay here.
void IRAM_ATTR Encoder::_isr() {
    if (!_instance) return;

    uint8_t a = digitalRead(_instance->_pinA);
    uint8_t b = digitalRead(_instance->_pinB);

    // Direction: on rising edge of A, if B is HIGH → CW, B is LOW → CCW
    // On falling edge of A, logic is reversed.
    if (a != _instance->_lastA) {
        if (a == HIGH) {
            _instance->_rawCount += (b == HIGH) ? +1 : -1;
        } else {
            _instance->_rawCount += (b == LOW)  ? +1 : -1;
        }
        _instance->_lastA = a;
    }
}

int8_t Encoder::getStep() {
    // Snapshot the volatile count atomically (disable ISR briefly)
    noInterrupts();
    int32_t current = _rawCount;
    interrupts();

    // How many raw pulses since last read?
    int32_t delta = current - _lastReportedCount;

    // EC11 gives 2 raw pulses per physical detent click.
    // We only report when a full detent has been crossed.
    if (delta >= PULSES_PER_DETENT) {
        // Advance by one full detent: leave any remainder for next call
        _lastReportedCount += PULSES_PER_DETENT;
        return +1;
    }
    if (delta <= -PULSES_PER_DETENT) {
        _lastReportedCount -= PULSES_PER_DETENT;
        return -1;
    }

    return 0;  // not yet a full detent
}

bool Encoder::wasPressed() {
    // Read raw button: LOW = pressed (because INPUT_PULLUP pulls HIGH at rest)
    bool rawPressed = (digitalRead(_pinBtn) == LOW);
    uint32_t now = millis();

    // Debounce: only react if state changed AND enough time has passed
    if (rawPressed != _lastBtnState && (now - _lastBtnTime) > DEBOUNCE_MS) {
        _lastBtnState = rawPressed;
        _lastBtnTime = now;

        // Only fire on the falling edge (press down, not release)
        if (rawPressed) {
            _pressedFlag = true;
        }
    }

    if (_pressedFlag) {
        _pressedFlag = false;  // clear immediately so next call returns false
        return true;
    }
    return false;
}