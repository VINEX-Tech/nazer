#include "../../lib/Vehicle/PowerPolicy.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace PowerPolicy;
    assert(recordInterval(false)==10000 && recordInterval(true)==20000);
    assert(uploadInterval(false,false)==60000 && uploadInterval(false,true)==300000);
    assert(uploadInterval(true,false)==1800000 && uploadInterval(true,true)==3600000);
    BatteryAlert battery;
    assert(!battery.update(-1));
    assert(!battery.update(50));
    assert(battery.update(20));
    assert(!battery.update(19));
    assert(!battery.update(30));
    assert(!battery.update(20));
    assert(!battery.update(31));
    assert(battery.update(20));
    StationaryTimer timer;
    assert(!timer.update(100,true));
    assert(!timer.update(300099,true));
    assert(timer.update(300100,true));
    assert(!timer.update(300101,false));
    assert(!timer.update(300102,true));
    timer.reset();
    assert(!timer.update(UINT32_MAX-1000,true));
    assert(timer.update(uint32_t(UINT32_MAX-1000+300000UL),true));
    std::cout<<"PASS: all six intervals, five-minute dwell, motion reset, millis rollover, battery crossing/re-arm/unknown\n";
}
