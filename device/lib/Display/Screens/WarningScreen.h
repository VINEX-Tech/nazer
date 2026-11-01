#pragma once
#include <Arduino.h>
// Flashing warning screen shown when speed > speed limit + margin
void drawWarningScreen(float speedKmh, int speedLimit, float excessKmh);
