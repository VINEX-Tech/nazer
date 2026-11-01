#pragma once
#include <Arduino.h>

class RoadLimits {
public:
    bool begin();
    int lookup(double lat, double lon, float headingDeg, float speedKmh);
    const char* packVersion() const { return _version[0] ? _version : nullptr; }
private:
    char _version[11] = {};
};
