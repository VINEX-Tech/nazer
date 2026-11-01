// Host check for BLE passkey validation and the bonding window (lib/Transport/BlePairingPolicy.h).
#include "../../lib/Transport/BlePairingPolicy.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace blepair;

    // Static passkey: fail closed on the placeholder and anything outside 1..999999.
    const long long bad[] = {0, -1, -999999, 1000000, 4294967295LL, 123456789};
    for (long long p : bad) assert(!validStaticPasskey(p));
    const long long good[] = {1, 42, 123456, 999999};
    for (long long p : good) assert(validStaticPasskey(p));

    // Random passkeys are always 1..999999, including the edges of the 32-bit range.
    const uint32_t samples[] = {0u, 1u, 999998u, 999999u, 1000000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (uint32_t r : samples) {
        uint32_t k = passkeyFromRandom(r);
        assert(k >= 1 && k <= MAX_PASSKEY);
        assert(validStaticPasskey(k));
    }
    assert(passkeyFromRandom(0) == 1);
    assert(passkeyFromRandom(999998) == 999999);
    assert(passkeyFromRandom(999999) == 1);

    // Window: closed until opened, open for exactly WINDOW_MS, reopens on demand.
    Window w;
    assert(!w.isOpen(0));
    w.open(1000);
    assert(w.isOpen(1000));
    assert(w.isOpen(1000 + WINDOW_MS - 1));
    assert(w.remainingMs(1000 + WINDOW_MS - 1) == 1);
    assert(!w.isOpen(1000 + WINDOW_MS));
    assert(!w.isOpen(1000));                 // stays closed, even if time looks earlier
    assert(w.remainingMs(5000) == 0);
    w.open(500000);
    assert(w.isOpen(500001));
    w.close();
    assert(!w.isOpen(500002));

    // Window across the millis() rollover.
    Window r;
    r.open(0xFFFFFF00u);
    assert(r.isOpen(0x00000100u));                       // 512 ms later, wrapped
    assert(!r.isOpen(0xFFFFFF00u + WINDOW_MS));

    // Long press fires once per hold, only after LONG_PRESS_MS, and not on short taps.
    LongPress lp;
    assert(!lp.update(true, 0));
    assert(!lp.update(true, LONG_PRESS_MS - 1));
    assert(lp.update(true, LONG_PRESS_MS));
    assert(!lp.update(true, LONG_PRESS_MS + 5000));     // still held: no repeat
    assert(!lp.update(false, LONG_PRESS_MS + 5001));
    assert(!lp.update(true, 20000));
    assert(!lp.update(false, 20000 + LONG_PRESS_MS - 1)); // released early
    assert(!lp.update(true, 0xFFFFFF00u));
    assert(lp.update(true, 0xFFFFFF00u + LONG_PRESS_MS)); // rollover-safe

    std::cout << "PASS: passkey range and placeholder, random mapping, window timing and rollover, long press\n";
    return 0;
}
