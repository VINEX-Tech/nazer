// =============================================================
// Storage.cpp: Implementation
// =============================================================

#include "Storage.h"
#include <Preferences.h>
#include <esp_efuse.h>
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>  // esp_efuse_mac_get_default on newer ESP-IDF
#endif

// Global singleton: matches extern declaration in Storage.h
StorageManager Storage;

// ============================================================
void StorageManager::begin() {
    // Verify the namespace is accessible. We open+close immediately -
    // actual data operations happen in each method call.
    Preferences p;
    if (!p.begin(NS_MAIN, false)) {
        Serial.println("[E][STORAGE] NVS namespace 'nazer' could not be opened");
        return;
    }
    p.end();
    _begun = true;
    Serial.println("[I][STORAGE] NVS ready (namespace: nazer)");
}

// ============================================================
String StorageManager::_generateDeviceId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[20];
    // Use last 4 bytes of MAC (bytes 2–5) for the ID suffix.
    // Bytes 0–1 are the OUI prefix and are the same for all
    // Espressif chips, so they add no uniqueness.
    snprintf(buf, sizeof(buf), "NAZER_%02X%02X%02X%02X",
             mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// ============================================================
String StorageManager::getDeviceId() {
    Preferences p;
    if (!p.begin(NS_MAIN, false)) return _generateDeviceId(); // NVS unavailable

    if (!p.isKey("device_id")) {
        // First boot: generate and persist the ID.
        String id = _generateDeviceId();
        p.putString("device_id", id);
        p.end();
        Serial.printf("[I][STORAGE] Device ID generated: %s\n", id.c_str());
        return id;
    }

    String id = p.getString("device_id", _generateDeviceId());
    p.end();
    return id;
}

// ============================================================
void StorageManager::setDeviceId(const String& id) {
    Preferences p;
    if (!p.begin(NS_MAIN, false)) return;
    p.putString("device_id", id);
    p.end();
}

// ============================================================
int StorageManager::getSpeedLimit(int fallback) {
    Preferences p;
    if (!p.begin(NS_MAIN, true)) return fallback; // no saved user override
    int v = p.getInt("speed_limit", fallback);
    p.end();
    return v;
}

// ============================================================
void StorageManager::setSpeedLimit(int limitKmh) {
    if (limitKmh < 10 || limitKmh > 200) return; // sanity guard
    Preferences p;
    if (!p.begin(NS_MAIN, false)) return;
    p.putInt("speed_limit", limitKmh);
    p.end();
}

// ============================================================
bool StorageManager::isCalibrationDone() {
    Preferences p;
    // true = read-only (don't create namespace if it doesn't exist yet)
    if (!p.begin(NS_IMU, true)) return false;
    bool v = p.getBool("cal_ok", false);
    p.end();
    return v;
}

bool StorageManager::getSaver() {
    Preferences p;
    if (!p.begin(NS_MAIN, true)) return true;
    bool value=p.getBool("saver", true); p.end(); return value;
}
void StorageManager::setSaver(bool enabled) {
    Preferences p;
    if (!p.begin(NS_MAIN, false)) return;
    p.putBool("saver", enabled); p.end();
}
