#pragma once
// config.h: every source file includes this.
//
// Real values (WiFi, device key, BLE pairing code, server host) go in
// include/config.local.h, which is gitignored. Copy config.example.h to
// config.local.h and edit it. Without a local file the build falls back to
// the placeholders in config.example.h, so a fresh clone still compiles.
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif
