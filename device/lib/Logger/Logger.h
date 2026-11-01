#pragma once
// ============================================================
// Logger.h: Serial debug output with log levels
// Levels: DEBUG < INFO < WARN < ERROR
// Set LOG_LEVEL in config.h to filter noisy output.
// In production, set LOG_LEVEL = LOG_LEVEL_ERROR.
// ============================================================

#include <Arduino.h>

// Log level constants: lower number = more verbose
#define LOG_LEVEL_DEBUG  0
#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_WARN   2
#define LOG_LEVEL_ERROR  3
#define LOG_LEVEL_NONE   4   // silence everything

// Default: show everything during development
#ifndef LOG_LEVEL
  #define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

// ---- Public macros ----
// Usage: LOG_INFO("GPS", "Fix acquired: %d sats", sats);
// Expands to a Serial.printf with timestamp + level tag.

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
  #define LOG_DEBUG(tag, fmt, ...) Logger::log('D', tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_DEBUG(tag, fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
  #define LOG_INFO(tag, fmt, ...) Logger::log('I', tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_INFO(tag, fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
  #define LOG_WARN(tag, fmt, ...) Logger::log('W', tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_WARN(tag, fmt, ...) do {} while(0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_ERROR
  #define LOG_ERROR(tag, fmt, ...) Logger::log('E', tag, fmt, ##__VA_ARGS__)
#else
  #define LOG_ERROR(tag, fmt, ...) do {} while(0)
#endif

// ---- Logger class ----
class Logger {
public:
    // Call once in setup() before any LOG_ macros
    static void begin(unsigned long baud = 115200);

    // Internal: used by macros: not for direct use
    static void log(char level, const char* tag, const char* fmt, ...);

private:
    static bool _ready;
};