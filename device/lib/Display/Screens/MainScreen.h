#pragma once
#include <Arduino.h>
// Shows: large speed, speed limit, GPS/GSM signal, battery, active transport
void drawMainScreen(float speedKmh, int speedLimit, uint8_t gpsSats, uint8_t gsmSignal,
                    uint8_t battPct, const char* transportLabel);
