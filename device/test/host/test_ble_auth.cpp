// Host check for the BLE pairing-code gate (lib/Transport/BleAuth.h).
#include "../../lib/Transport/BleAuth.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    // Placeholder, short, missing and over-long codes all fail closed.
    const char* longCode = "0123456789012345678901234567890123456789012345678901234567890123456789";
    const char* unusable[] = {nullptr, "", "short", BleAuth::placeholder(), longCode};
    for (const char* code : unusable) {
        BleAuth a(code);
        assert(!a.configured());
        assert(a.check(code, 0) == BleAuth::NOT_CONFIGURED);
        assert(a.check("anything-at-all", 0) == BleAuth::NOT_CONFIGURED);
    }

    BleAuth auth("pair-code-1234");
    assert(auth.configured());
    assert(auth.check("pair-code-1234", 0) == BleAuth::OK);
    assert(auth.check("pair-code-123", 0) == BleAuth::WRONG);    // prefix
    assert(auth.check("pair-code-12345", 0) == BleAuth::WRONG);  // longer
    assert(auth.check("PAIR-CODE-1234", 0) == BleAuth::WRONG);   // case
    assert(auth.check(nullptr, 0) == BleAuth::WRONG);
    assert(auth.check("pair-code-1234", 0) == BleAuth::OK);      // success resets the count
    assert(auth.failures() == 0);

    // Five wrong codes lock it; even the right code is refused while locked.
    for (int i = 0; i < 4; i++) assert(auth.check("nope", 1000) == BleAuth::WRONG);
    assert(auth.check("nope", 1000) == BleAuth::WRONG);
    assert(auth.check("pair-code-1234", 1001) == BleAuth::LOCKED);
    assert(auth.check("pair-code-1234", 1000 + BleAuth::LOCKOUT_MS - 1) == BleAuth::LOCKED);
    assert(auth.check("pair-code-1234", 1000 + BleAuth::LOCKOUT_MS) == BleAuth::OK);

    // Lockout across the 32-bit millis() rollover.
    BleAuth wrap("pair-code-1234");
    const uint32_t nearEnd = 0xFFFFFF00u;
    for (int i = 0; i < 5; i++) wrap.check("nope", nearEnd);
    assert(wrap.check("pair-code-1234", nearEnd + 1000u) == BleAuth::LOCKED);   // wrapped, 1 s later
    assert(wrap.check("pair-code-1234", nearEnd + BleAuth::LOCKOUT_MS) == BleAuth::OK);

    // Only "auth" passes before the session is authorised.
    const char* gated[] = {"set_limit", "set_mode", "set_contacts", "set_saver", "cancel_crash",
                           "calibrate_imu", "get_config", "", "AUTH"};
    for (const char* cmd : gated) {
        assert(!BleAuth::allowed(cmd, false));
        assert(BleAuth::allowed(cmd, true));
    }
    assert(BleAuth::allowed("auth", false));
    assert(!BleAuth::allowed(nullptr, false));

    std::cout << "PASS: fail-closed codes, exact match, 5-try lockout, rollover, command gate\n";
    return 0;
}
