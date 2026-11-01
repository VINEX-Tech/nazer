// ============================================================
// lib/Vehicle/VehicleConfig.h: vehicle mode, emergency contacts,
// and the per-mode thresholds that depend on them.
//
// Persisted in the existing "nazer" NVS namespace under new keys
// ("veh_mode", "contact0".."contact2"); the speed limit itself stays
// in Storage ("speed_limit") so older firmware keeps reading it.
// ============================================================
#pragma once
#include <Arduino.h>

enum class VehicleMode : uint8_t { MOTORBIKE = 0, SCOOTER = 1, CAR = 2 };

struct CrashThresholds {
    float impactG;        // |a| peak that opens a crash candidate
    float tiltDeg;        // orientation change that confirms a fall
    float minSpeedKmh;    // speed before impact needed for the "sudden stop" path
};

class VehicleConfig {
public:
    static constexpr uint8_t MAX_CONTACTS = 3;

    void begin();

    VehicleMode mode() const { return _mode; }
    const char* modeName() const { return modeToName(_mode); }
    bool        setModeByName(const char* name);   // false if unknown

    uint8_t     contactCount() const { return _contactCount; }
    const char* contact(uint8_t i) const { return i < _contactCount ? _contacts[i] : ""; }
    bool        isContact(const char* number) const;   // matches local or +20 format
    // Replaces all contacts. Invalid numbers are skipped; returns how many were kept.
    uint8_t     setContacts(const char* const* numbers, uint8_t count);

    CrashThresholds crashThresholds() const;
    int defaultLimitKmh() const;

    static const char* modeToName(VehicleMode m);
    // Accepts 01XXXXXXXXX, +201XXXXXXXXX or 201XXXXXXXXX; writes local format.
    static bool normalizeNumber(const char* in, char out[12]);

private:
    VehicleMode _mode = VehicleMode::MOTORBIKE;
    char        _contacts[MAX_CONTACTS][12] = {};
    uint8_t     _contactCount = 0;
    void _save();
};

extern VehicleConfig Vehicle;
