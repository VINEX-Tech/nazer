// ============================================================
// Display.cpp
// ============================================================

#include "Display.h"
#include "../../include/config.h"

// ---- Global singleton definition ----
DisplayDriver g_display(OLED_I2C_ADDR, SDA_PIN, SCL_PIN);

// ============================================================
// Constructor + lifecycle
// ============================================================
DisplayDriver::DisplayDriver(uint8_t addr, uint8_t sda, uint8_t scl)
    : _oled(addr, sda, scl)
{}

bool DisplayDriver::begin() {
    _oled.init();
    _oled.flipScreenVertically();
    _oled.setFont(ArialMT_Plain_10);
    _oled.clear();
    _oled.display();
    return true;
}

// ============================================================
// LOW-LEVEL primitives
// ============================================================

void DisplayDriver::clear() {
    _oled.clear();
    // Intentionally does NOT call _oled.display() -
    // caller must call flush() after all drawing is done.
}

void DisplayDriver::flush() {
    _oled.display();
}

void DisplayDriver::drawString(int x, int y, const char* text) {
    _oled.setFont(ArialMT_Plain_10);
    _oled.setTextAlignment(TEXT_ALIGN_LEFT);
    _oled.drawString(x, y, text);
}

void DisplayDriver::drawLargeNumber(int x, int y, int value) {
    _oled.setFont(ArialMT_Plain_24);
    _oled.setTextAlignment(TEXT_ALIGN_LEFT);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", value);
    _oled.drawString(x, y, buf);
}

void DisplayDriver::drawLine(int x0, int y0, int x1, int y1) {
    _oled.drawLine(x0, y0, x1, y1);
}

void DisplayDriver::drawRect(int x, int y, int w, int h, bool filled) {
    if (filled) _oled.fillRect(x, y, w, h);
    else        _oled.drawRect(x, y, w, h);
}

void DisplayDriver::invertDisplay(bool invert) {
    // ThingPulse's invertDisplay() is a toggle with no argument.
    // We track state ourselves and only call it when the desired
    // state differs from the current state.
    if (invert != _inverted) {
        _oled.invertDisplay();
        _inverted = invert;
    }
}

// ============================================================
// HIGH-LEVEL screens
// ============================================================

void DisplayDriver::showSplash() {
    _oled.clear();
    _oled.setFont(ArialMT_Plain_24);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    _oled.drawString(64, 10, "NAZER");
    _oled.setFont(ArialMT_Plain_10);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    _oled.drawString(64, 40, "Speed Monitor v2.0");
    _oled.drawString(64, 54, "Initializing...");
    _oled.display();
}

void DisplayDriver::showCounter(int32_t counter, uint32_t presses) {
    _oled.clear();

    _oled.setFont(ArialMT_Plain_10);
    _oled.setTextAlignment(TEXT_ALIGN_LEFT);
    _oled.drawString(0, 0, "NAZER Phase 1 Test");
    _oled.drawLine(0, 12, 127, 12);

    _oled.setFont(ArialMT_Plain_24);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%ld", counter);
    _oled.drawString(64, 18, countStr);

    _oled.setFont(ArialMT_Plain_10);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    _oled.drawString(64, 44, "CW: +1  |  CCW: -1");

    _oled.setTextAlignment(TEXT_ALIGN_RIGHT);
    char btnStr[24];
    snprintf(btnStr, sizeof(btnStr), "BTN: %lu", presses);
    _oled.drawString(127, 54, btnStr);

    _oled.display();
}

void DisplayDriver::showError(const char* msg) {
    _oled.clear();
    _oled.fillRect(0, 0, 128, 20);
    _oled.setColor(BLACK);
    _oled.setFont(ArialMT_Plain_16);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    _oled.drawString(64, 2, "ERROR");
    _oled.setColor(WHITE);
    _oled.setFont(ArialMT_Plain_10);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    _oled.drawStringMaxWidth(0, 24, 128, msg);
    _oled.display();
}

void DisplayDriver::showMessage(const char* line1, const char* line2) {
    _oled.clear();
    _oled.setFont(ArialMT_Plain_16);
    _oled.setTextAlignment(TEXT_ALIGN_CENTER);
    if (line2 == nullptr) {
        _oled.drawString(64, 24, line1);
    } else {
        _oled.drawString(64, 14, line1);
        _oled.setFont(ArialMT_Plain_10);
        _oled.drawString(64, 38, line2);
    }
    _oled.display();
}

void DisplayDriver::_drawBar(int x, int y, int w, int h, int pct) {
    _oled.drawRect(x, y, w, h);
    if (pct > 0) {
        int fillW = (w - 2) * constrain(pct, 0, 100) / 100;
        if (fillW > 0) _oled.fillRect(x + 1, y + 1, fillW, h - 2);
    }
}