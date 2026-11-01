// Host check for the device-claim challenge answer (lib/Transport/ClaimResponse.h).
// Expected values come from Python's hmac/hashlib, the same code the server uses.
#include "../../lib/Transport/ClaimResponse.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

static std::string hex(const uint8_t* b, size_t n) {
    std::string s; char t[3];
    for (size_t i = 0; i < n; i++) { snprintf(t, sizeof(t), "%02x", b[i]); s += t; }
    return s;
}

int main() {
    // FIPS 180-4 "abc"
    claim::Sha256 s; s.update((const uint8_t*)"abc", 3); uint8_t d[32]; s.final(d);
    assert(hex(d, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // RFC 4231 test case 2
    uint8_t mac[32];
    const char* msg = "what do ya want for nothing?";
    claim::hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)msg, strlen(msg), nullptr, 0, mac);
    assert(hex(mac, 32) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    // Key longer than a block, message spread over several blocks and two parts.
    std::string key(100, 'k'), m(130, 'x');
    claim::hmacSha256((const uint8_t*)key.data(), key.size(), (const uint8_t*)m.data(), 70,
                      (const uint8_t*)m.data() + 70, 60, mac);
    assert(hex(mac, 32) == "d849e93bb3ac1ff095b92b63718bafe2701ea803a8e68087e1262d1b245e6765");

    // The claim answer matches the server's formula.
    char out[65];
    assert(claim::response("fixture-input", "0123456789abcdef0123456789abcdef", "NAZER_0B49EFD0", out, sizeof(out)));
    assert(std::string(out) == "cf39428055772745f8140470d732a97114a42f7b6282a1a2099ad948473ae8ae");

    // Bad nonces and small buffers are refused.
    const char* bad[] = {nullptr, "", "abc", "0123456789ABCDEF0123456789ABCDEF",
                         "0123456789abcdef0123456789abcdeg",
                         "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0"};
    for (const char* n : bad) assert(!claim::response("k", n, "D", out, sizeof(out)));
    assert(!claim::response("k", "0123456789abcdef0123456789abcdef", "D", out, 64));

    std::cout << "PASS: SHA-256 and RFC 4231 vectors, server-matching claim answer, bad nonces refused\n";
    return 0;
}
