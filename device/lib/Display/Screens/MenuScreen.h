#pragma once
#include <Arduino.h>
// Simple scrolling menu. items[] = array of string pointers, selectedIdx = highlighted row.
void drawMenuScreen(const char** items, uint8_t count, uint8_t selectedIdx);
