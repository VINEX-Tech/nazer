#pragma once
// =============================================================
// Storage.h: NVS wrapper for device ID, speed limit, IMU check
//
// Preferences.h and esp_efuse.h are intentionally NOT included
// here: they are ESP32 framework headers that must only appear
// in .cpp files. Including them in a .h causes "No such file"
// errors when this header is pulled in by Transport library files
// that compile before the framework path is fully resolved.
// =============================================================

#include <Arduino.h>   // String, uint8_t: safe everywhere

class StorageManager {
public:
    void   begin();
    String getDeviceId();
    void   setDeviceId(const String& id);
    bool   getSaver();
    void   setSaver(bool enabled);
    int    getSpeedLimit(int fallback = 80);
    void   setSpeedLimit(int limitKmh);
    bool   isCalibrationDone();

private:
    static constexpr const char* NS_MAIN = "nazer";
    static constexpr const char* NS_IMU  = "nazer_imu";
    bool _begun = false;
    static String _generateDeviceId();
};

extern StorageManager Storage;