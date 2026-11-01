#include "VehicleConfig.h"
#include <Preferences.h>

VehicleConfig Vehicle;

static constexpr const char* NS_MAIN = "nazer";

void VehicleConfig::begin() {
    Preferences p;
    p.begin(NS_MAIN, true);
    uint8_t m = p.getUChar("veh_mode", (uint8_t)VehicleMode::MOTORBIKE);
    _mode = m <= (uint8_t)VehicleMode::CAR ? (VehicleMode)m : VehicleMode::MOTORBIKE;
    _contactCount = 0;
    for (uint8_t i = 0; i < MAX_CONTACTS; i++) {
        char key[10];
        snprintf(key, sizeof(key), "contact%u", i);
        String v = p.isKey(key) ? p.getString(key, "") : String();
        if (v.length() == 11) {
            strncpy(_contacts[_contactCount], v.c_str(), 11);
            _contacts[_contactCount][11] = '\0';
            _contactCount++;
        }
    }
    p.end();
    Serial.printf("[I][VEHICLE] mode=%s contacts=%u\n", modeName(), _contactCount);
}

bool VehicleConfig::setModeByName(const char* name) {
    VehicleMode m;
    if      (strcmp(name, "motorbike") == 0) m = VehicleMode::MOTORBIKE;
    else if (strcmp(name, "scooter")   == 0) m = VehicleMode::SCOOTER;
    else if (strcmp(name, "car")       == 0) m = VehicleMode::CAR;
    else return false;
    _mode = m;
    _save();
    return true;
}

uint8_t VehicleConfig::setContacts(const char* const* numbers, uint8_t count) {
    _contactCount = 0;
    for (uint8_t i = 0; i < count && _contactCount < MAX_CONTACTS; i++) {
        if (normalizeNumber(numbers[i], _contacts[_contactCount])) _contactCount++;
    }
    _save();
    return _contactCount;
}

bool VehicleConfig::isContact(const char* number) const {
    char local[12];
    if (!normalizeNumber(number, local)) return false;
    for (uint8_t i = 0; i < _contactCount; i++) {
        if (strcmp(_contacts[i], local) == 0) return true;
    }
    return false;
}

CrashThresholds VehicleConfig::crashThresholds() const {
    // Starting points, to be tuned from field logs. Two-wheelers fall over
    // after an impact, so tilt is the strong signal; cars stay upright, so
    // a car relies on a harder impact plus a sudden stop.
    switch (_mode) {
        case VehicleMode::SCOOTER:   return {3.0f, 55.0f, 10.0f};
        case VehicleMode::CAR:       return {4.0f, 60.0f, 20.0f};
        case VehicleMode::MOTORBIKE:
        default:                     return {3.5f, 55.0f, 15.0f};
    }
}

int VehicleConfig::defaultLimitKmh() const {
    switch (_mode) {
        case VehicleMode::SCOOTER: return 50;
        case VehicleMode::CAR:     return 100;
        default:                   return 80;
    }
}

const char* VehicleConfig::modeToName(VehicleMode m) {
    switch (m) {
        case VehicleMode::SCOOTER: return "scooter";
        case VehicleMode::CAR:     return "car";
        default:                   return "motorbike";
    }
}

bool VehicleConfig::normalizeNumber(const char* in, char out[12]) {
    char digits[16];
    size_t n = 0;
    for (const char* p = in; *p && n < sizeof(digits) - 1; p++) {
        if (*p >= '0' && *p <= '9') digits[n++] = *p;
    }
    digits[n] = '\0';
    const char* local = digits;
    // Drop the country code but keep its trailing 0 as the local trunk 0:
    // 201XXXXXXXXX → 01XXXXXXXXX, 00201XXXXXXXXX → 01XXXXXXXXX
    if (n == 12 && strncmp(digits, "20", 2) == 0) local = digits + 1;
    else if (n == 14 && strncmp(digits, "0020", 4) == 0) local = digits + 3;
    if (strlen(local) != 11 || local[0] != '0' || local[1] != '1') return false;
    strncpy(out, local, 11);
    out[11] = '\0';
    return true;
}

void VehicleConfig::_save() {
    Preferences p;
    p.begin(NS_MAIN, false);
    p.putUChar("veh_mode", (uint8_t)_mode);
    for (uint8_t i = 0; i < MAX_CONTACTS; i++) {
        char key[10];
        snprintf(key, sizeof(key), "contact%u", i);
        if (i < _contactCount) p.putString(key, _contacts[i]);
        else p.remove(key);
    }
    p.end();
}
