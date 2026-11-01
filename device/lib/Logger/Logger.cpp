// ============================================================
// Logger.cpp: Implementation
// Format: [ms_since_boot][LEVEL][TAG] message
// Example: [  1234][I][GPS] Fix acquired: 8 sats
// ============================================================

#include "Logger.h"
#include <stdarg.h>

bool Logger::_ready = false;

void Logger::begin(unsigned long baud) {
    Serial.begin(baud);
    // Wait up to 2 seconds for Serial to be ready (USB CDC on ESP32)
    unsigned long t = millis();
    while (!Serial && (millis() - t) < 2000) { delay(10); }
    _ready = true;
    Serial.println();
    Serial.println(F("=== NAZER Logger Started ==="));
}

void Logger::log(char level, const char* tag, const char* fmt, ...) {
    if (!_ready) return;

    // Timestamp: pad to 7 digits so columns line up nicely
    Serial.printf("[%7lu]", millis());

    // Level character with square bracket
    Serial.printf("[%c]", level);

    // Module tag, fixed-width 8 chars so values align
    Serial.printf("[%-8s] ", tag);

    // The actual message with printf-style formatting
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Serial.println(buf);
}