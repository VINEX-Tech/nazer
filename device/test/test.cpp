// ============================================================
// test/test_at_passthrough.cpp: Auto AT diagnostic for SIM800L
//
// In platformio.ini:
//   build_src_filter = -<*> +<../test/test_at_passthrough.cpp>
//
// Runs all diagnostic commands automatically on boot.
// Just flash and open Serial Monitor at 115200.
// ============================================================

#include <Arduino.h>
#include "config.h"
#include <HardwareSerial.h>