#pragma once
#include <cstdint>

namespace PowerPolicy {
inline uint32_t recordInterval(bool saver) { return saver ? 20000UL : 10000UL; }
inline uint32_t uploadInterval(bool parked, bool saver) {
    return parked ? (saver ? 3600000UL : 1800000UL) : (saver ? 300000UL : 60000UL);
}
class BatteryAlert {
    int previous=-1;
    bool armed=true;
public:
    bool update(int percent) {
        if (percent<0 || percent>100) return false;
        if (percent>30) armed=true;
        bool crossed=armed && previous>20 && percent<=20;
        if (crossed) armed=false;
        previous=percent;
        return crossed;
    }
};
class StationaryTimer {
    uint32_t since=0;
    bool timing=false;
public:
    bool update(uint32_t now, bool stationary) {
        if (!stationary) { timing=false; return false; }
        if (!timing) { since=now; timing=true; }
        return uint32_t(now-since)>=300000UL;
    }
    void reset() { timing=false; }
};
}
