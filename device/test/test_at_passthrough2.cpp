// ============================================================
// test/test_at_passthrough2.cpp: SIM800L Diagnostic (fixed timing)
//
// In platformio.ini:
//   build_src_filter = -<*> +<../test/test_at_passthrough2.cpp>
//
// Fixes timing issues from test_at_passthrough.cpp:
//   - Full flush + 500ms settle between every command
//   - 8 second timeout for CPIN? (SIM can be slow)
//   - Waits for OK/ERROR before moving to next command
// ============================================================

#include <Arduino.h>
#include "config.h"
#include <HardwareSerial.h>

static HardwareSerial gsmSerial(2);

// -----------------------------------------------------------------------
// Flush ALL pending bytes, wait until line is fully silent for quietMs
// -----------------------------------------------------------------------
static void fullFlush(uint32_t quietMs = 500) {
    uint32_t lastByte = millis();
    while (millis() - lastByte < quietMs) {
        if (gsmSerial.available()) {
            gsmSerial.read();
            lastByte = millis();
        }
    }
}

// -----------------------------------------------------------------------
// Send a command, wait up to timeoutMs, print raw response
// -----------------------------------------------------------------------
static bool probe(const char* label, const char* cmd, uint32_t timeoutMs = 5000) {
    fullFlush(300);   // silence line before sending

    Serial.printf("\n>>> [%s]  CMD: %s\n", label, cmd);
    gsmSerial.println(cmd);

    char buf[512];
    size_t pos = 0;
    uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        while (gsmSerial.available() && pos < sizeof(buf) - 1) {
            buf[pos++] = (char)gsmSerial.read();
            buf[pos]   = '\0';
        }
        if (strstr(buf, "OK") || strstr(buf, "ERROR") || strstr(buf, "READY")) break;
        delay(5);
    }

    buf[pos] = '\0';
    bool ok = (strstr(buf, "OK") != nullptr) || (strstr(buf, "READY") != nullptr);

    Serial.printf("<<< RAW: [%s]\n", buf);
    Serial.printf("    %s\n", ok ? "✓ OK" : "✗ No OK/READY in response");

    delay(200);  // settle before next command
    return ok;
}

// -----------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(2000);  // give Serial Monitor time to connect

    Serial.println("\n\n============================================================");
    Serial.println("  SIM800L Diagnostic v2 (fixed timing)");
    Serial.printf ("  UART2  RX=GPIO%d  TX=GPIO%d  BAUD=%d\n",
                   GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD);
    Serial.println("============================================================");

    gsmSerial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(1000);
    fullFlush(1000);  // eat any boot noise from module

    // ---- 1. Basic comms ----
    Serial.println("\n---- BASIC COMMS ----");
    probe("1. AT",        "AT",    3000);
    probe("2. ATE0",      "ATE0",  3000);
    probe("3. AT again",  "AT",    3000);

    // ---- 2. SIM: give it 8 seconds ----
    Serial.println("\n---- SIM STATUS (up to 8s) ----");
    probe("4. CPIN",  "AT+CPIN?",  8000);
    probe("5. CCID",  "AT+CCID",   5000);
    probe("6. CIMI",  "AT+CIMI",   5000);

    // ---- 3. Network ----
    Serial.println("\n---- NETWORK ----");
    probe("7. CREG",   "AT+CREG?",  5000);
    probe("8. CSQ",    "AT+CSQ",    3000);
    probe("9. COPS",   "AT+COPS?",  8000);  // which operator is registered

    // ---- 4. Firmware ----
    Serial.println("\n---- MODULE INFO ----");
    probe("10. GMR",   "AT+GMR",    3000);
    probe("11. GSN",   "AT+GSN",    3000);  // IMEI

    // ---- Summary ----
    Serial.println("\n============================================================");
    Serial.println("  PASS CRITERIA:");
    Serial.println("    #4 CPIN  → +CPIN: READY");
    Serial.println("    #5 CCID  → 20-digit ICCID number");
    Serial.println("    #6 CIMI  → 15-digit IMSI number");
    Serial.println("    #7 CREG  → +CREG: 0,1 or 0,5  (registered)");
    Serial.println("    #8 CSQ   → +CSQ: X,Y  where X is 5..30");
    Serial.println("    #9 COPS  → operator name (e.g. Vodafone EG)");
    Serial.println("============================================================\n");
}

void loop() {
    // Bridge mode: type AT commands manually if needed
    while (gsmSerial.available()) Serial.write(gsmSerial.read());
    while (Serial.available())    gsmSerial.write(Serial.read());
}