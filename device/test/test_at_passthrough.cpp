// ============================================================
// test/test_at_passthrough.cpp: Auto AT diagnostic for SIM800L
//
// In platformio.ini:
//   build_src_filter = -<*> +<../test/test_at_passthrough.cpp>
//
// Runs all diagnostic commands automatically on boot.
// Just flash and open Serial Monitor at 115200.
// ============================================================

#include <Arduino.h>
#include "config.h"
#include <HardwareSerial.h>

static HardwareSerial gsmSerial(2);

// Send a command, wait up to timeoutMs, print raw response with label
static void probe(const char* label, const char* cmd, uint32_t timeoutMs = 3000) {
    // Flush
    while (gsmSerial.available()) gsmSerial.read();

    Serial.printf("\n>>> %s : %s\n", label, cmd);
    gsmSerial.println(cmd);

    char buf[256];
    size_t pos = 0;
    uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        while (gsmSerial.available() && pos < sizeof(buf) - 1) {
            buf[pos++] = (char)gsmSerial.read();
            buf[pos]   = '\0';
        }
        // Stop early if we see OK or ERROR
        if (strstr(buf, "OK") || strstr(buf, "ERROR")) break;
        delay(1);
    }

    buf[pos] = '\0';
    Serial.printf("<<< RAW: [%s]\n", buf);
}

void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("\n\n============================================================");
    Serial.println("  SIM800L Auto-Diagnostic");
    Serial.printf ("  UART2  RX=GPIO%d  TX=GPIO%d  BAUD=%d\n",
                   GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD);
    Serial.println("============================================================");

    gsmSerial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(500);

    // Flush any boot noise from module
    while (gsmSerial.available()) gsmSerial.read();
    delay(200);

    // ---- Basic comms ----
    probe("1. ECHO CHECK  ", "AT");
    probe("2. ECHO OFF    ", "ATE0");
    probe("3. ECHO CHECK2 ", "AT");

    // ---- SIM status: the key one ----
    probe("4. SIM STATUS  ", "AT+CPIN?",  5000);

    // ---- SIM identity: fails if SIM not readable ----
    probe("5. SIM ICCID   ", "AT+CCID",   5000);
    probe("6. SIM IMSI    ", "AT+CIMI",   5000);

    // ---- Network ----
    probe("7. NET REGISTER", "AT+CREG?",  3000);
    probe("8. SIGNAL      ", "AT+CSQ",    3000);

    // ---- Firmware info ----
    probe("9. FIRMWARE    ", "AT+GMR",    3000);

    Serial.println("\n============================================================");
    Serial.println("  Diagnostic complete.");
    Serial.println("  Expected for working SIM:");
    Serial.println("    #4 → +CPIN: READY");
    Serial.println("    #5 → 20-digit number");
    Serial.println("    #6 → 15-digit number");
    Serial.println("    #7 → +CREG: 0,1  or  0,5");
    Serial.println("  If #4-6 all show ERROR → SIM not seated / bad contact");
    Serial.println("  If #4 shows +CPIN: SIM PIN → PIN lock, need AT+CPIN=XXXX");
    Serial.println("============================================================\n");
}

void loop() {
    // Keep UART bridge open so you can still type commands manually if needed
    while (gsmSerial.available()) Serial.write(gsmSerial.read());
    while (Serial.available())    gsmSerial.write(Serial.read());
}