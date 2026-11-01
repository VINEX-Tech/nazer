// NAZER hardware bring-up: checks every module on the PCB and streams a status line.
//
// Serial commands (115200, newline terminated):
//   at <cmd>          send a raw AT command to the SIM800L and print the reply
//   sms <num> <text>  send an SMS
//   raw               toggle raw NMEA echo from the GPS
//   imu               toggle fast IMU stream (10 Hz)
//   status            print the full report now

#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include "config.h"

HardwareSerial gpsSerial(1);
HardwareSerial gsmSerial(2);
TinyGPSPlus gps;

static bool rawNmea = false;
static bool imuFast = false;
static bool imuOk = false;
static int gpsRx = GPS_RX_PIN, gpsTx = GPS_TX_PIN;
static uint32_t gpsBaud = GPS_BAUD;
static uint32_t gpsBytes = 0;
static bool gsmOk = false;
static uint32_t gsmBaud = GSM_BAUD;

// ---------------------------------------------------------------- GSM helpers
static String gsmCmd(const String &cmd, uint32_t timeoutMs = 2000, const char *until = "OK") {
    while (gsmSerial.available()) gsmSerial.read();
    gsmSerial.print(cmd);
    gsmSerial.print("\r");
    String resp;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
        while (gsmSerial.available()) resp += (char)gsmSerial.read();
        if (resp.indexOf(until) >= 0 || resp.indexOf("ERROR") >= 0) break;
        delay(5);
    }
    resp.trim();
    return resp;
}

static bool gsmProbe() {
    const uint32_t bauds[] = {GSM_BAUD, 115200, 57600, 19200, 38400};
    for (uint32_t b : bauds) {
        gsmSerial.begin(b, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
        delay(50);
        for (int i = 0; i < 3; i++) {
            if (gsmCmd("AT", 500).indexOf("OK") >= 0) {
                gsmBaud = b;
                return true;
            }
        }
    }
    return false;
}

static void gsmBringUp() {
    pinMode(GSM_RST_PIN, OUTPUT);
    pinMode(GSM_PWRKEY_PIN, OUTPUT);
    digitalWrite(GSM_RST_PIN, HIGH);
    digitalWrite(GSM_PWRKEY_PIN, HIGH);

    Serial.println("[GSM] probing at current power state...");
    gsmOk = gsmProbe();
    if (!gsmOk) {
        Serial.println("[GSM] no reply, pulsing PWRKEY then RST");
        digitalWrite(GSM_PWRKEY_PIN, LOW); delay(1200); digitalWrite(GSM_PWRKEY_PIN, HIGH);
        delay(4000);
        gsmOk = gsmProbe();
    }
    if (!gsmOk) {
        digitalWrite(GSM_RST_PIN, LOW); delay(150); digitalWrite(GSM_RST_PIN, HIGH);
        delay(5000);
        gsmOk = gsmProbe();
    }
    if (!gsmOk) {
        Serial.println("[GSM] FAIL: no AT reply on any baud (check 4.0V rail, RX/TX, antenna)");
        return;
    }
    Serial.printf("[GSM] OK at %lu baud\n", gsmBaud);
    gsmCmd("ATE0");
    gsmCmd("AT+CMEE=2");
    const char *info[] = {"ATI", "AT+CGMR", "AT+CPIN?", "AT+CCID", "AT+CBC", "AT+CSQ", "AT+CREG?", "AT+COPS?"};
    for (const char *c : info) {
        Serial.printf("[GSM] %-9s -> %s\n", c, gsmCmd(c, 3000).c_str());
    }
}

// ---------------------------------------------------------------- GPS helpers
static uint32_t gpsListen(int rx, int tx, uint32_t baud, uint32_t ms, bool *sawNmea) {
    gpsSerial.end();
    gpsSerial.begin(baud, SERIAL_8N1, rx, tx);
    uint32_t n = 0, t0 = millis();
    String line;
    *sawNmea = false;
    while (millis() - t0 < ms) {
        while (gpsSerial.available()) {
            char c = gpsSerial.read();
            n++;
            line += c;
            if (line.length() > 6) line.remove(0, line.length() - 6);
            if (line.indexOf("$GP") >= 0 || line.indexOf("$GN") >= 0 || line.indexOf("$BD") >= 0) *sawNmea = true;
        }
    }
    return n;
}

static void gpsBringUp() {
    struct Try { int rx, tx; uint32_t baud; };
    const Try tries[] = {
        {GPS_RX_PIN, GPS_TX_PIN, 9600}, {GPS_TX_PIN, GPS_RX_PIN, 9600},
        {GPS_RX_PIN, GPS_TX_PIN, 115200}, {GPS_RX_PIN, GPS_TX_PIN, 38400},
    };
    for (const Try &t : tries) {
        bool nmea;
        uint32_t n = gpsListen(t.rx, t.tx, t.baud, 2500, &nmea);
        Serial.printf("[GPS] RX=%d TX=%d %lu baud: %lu bytes, NMEA=%s\n", t.rx, t.tx, t.baud, n, nmea ? "yes" : "no");
        if (nmea) {
            gpsRx = t.rx; gpsTx = t.tx; gpsBaud = t.baud;
            gpsSerial.end();
            gpsSerial.begin(gpsBaud, SERIAL_8N1, gpsRx, gpsTx);
            Serial.println("[GPS] OK: module is talking");
            return;
        }
    }
    gpsSerial.end();
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.println("[GPS] FAIL: no NMEA on any pin/baud (check 3.3V, TX->GPIO4)");
}

// ---------------------------------------------------------------- IMU helpers
static void mpuWrite(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(0x68); Wire.write(reg); Wire.write(v); Wire.endTransmission();
}

static bool mpuRead(int16_t out[7]) {
    Wire.beginTransmission(0x68);
    Wire.write(0x3B);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)0x68, (uint8_t)14) != 14) return false;
    for (int i = 0; i < 7; i++) out[i] = (Wire.read() << 8) | Wire.read();
    return true;
}

static void imuBringUp() {
    Wire.begin(SDA_PIN, SCL_PIN, 400000);
    Serial.print("[I2C] devices:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); found++; }
    }
    Serial.println(found ? "" : " none");

    Wire.beginTransmission(0x68); Wire.write(0x75); Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)0x68, (uint8_t)1);
    uint8_t who = Wire.available() ? Wire.read() : 0;
    Serial.printf("[IMU] WHO_AM_I = 0x%02X\n", who);
    if (who != 0x68 && who != 0x70 && who != 0x71 && who != 0x72) {
        Serial.println("[IMU] FAIL: MPU not answering at 0x68");
        return;
    }
    mpuWrite(0x6B, 0x00);   // wake
    mpuWrite(0x1B, 0x00);   // gyro +-250 dps
    mpuWrite(0x1C, 0x00);   // accel +-2 g
    mpuWrite(0x1A, 0x03);   // DLPF 44 Hz
    delay(50);
    imuOk = true;
    Serial.println("[IMU] OK");
}

// ---------------------------------------------------------------- reporting
static float batteryVolts() {
    uint32_t mv = 0;
    for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(BATTERY_ADC_PIN);
    return (mv / 16.0f / 1000.0f) * (100.0f + 47.0f) / 47.0f;
}

static void printImu() {
    int16_t r[7];
    if (!mpuRead(r)) { Serial.println("[IMU] read failed"); return; }
    float ax = r[0] / 16384.0f, ay = r[1] / 16384.0f, az = r[2] / 16384.0f;
    float g = sqrtf(ax * ax + ay * ay + az * az);
    Serial.printf("[IMU] a=(%.2f %.2f %.2f) |a|=%.2fg  gyro=(%.1f %.1f %.1f)dps  T=%.1fC\n",
                  ax, ay, az, g, r[4] / 131.0f, r[5] / 131.0f, r[6] / 131.0f, r[3] / 340.0f + 36.53f);
}

static void printGps() {
    Serial.printf("[GPS] bytes=%lu sentences=%lu fail=%lu sats=%s hdop=%s fix=%s",
                  gpsBytes, gps.passedChecksum(), gps.failedChecksum(),
                  gps.satellites.isValid() ? String(gps.satellites.value()).c_str() : "-",
                  gps.hdop.isValid() ? String(gps.hdop.hdop(), 1).c_str() : "-",
                  gps.location.isValid() ? "YES" : "no");
    if (gps.location.isValid()) {
        Serial.printf(" lat=%.6f lon=%.6f speed=%.1fkm/h course=%.0f age=%lums",
                      gps.location.lat(), gps.location.lng(), gps.speed.kmph(), gps.course.deg(), gps.location.age());
    }
    if (gps.time.isValid()) Serial.printf(" utc=%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    Serial.println();
}

static void printGsm() {
    if (!gsmOk) { Serial.println("[GSM] not available"); return; }
    Serial.printf("[GSM] %s | %s\n", gsmCmd("AT+CSQ").c_str(), gsmCmd("AT+CREG?").c_str());
}

static void printStatus() {
    Serial.println("------------------------------------------------");
    printGps();
    if (imuOk) printImu();
    printGsm();
    Serial.printf("[BAT] %.2f V (raw divider estimate)\n", batteryVolts());
}

// ---------------------------------------------------------------- commands
static void handleCommand(String line) {
    line.trim();
    if (line.startsWith("at ")) {
        String cmd = line.substring(3);
        Serial.printf("[GSM] %s -> %s\n", cmd.c_str(), gsmCmd(cmd, 10000).c_str());
    } else if (line.startsWith("sms ")) {
        int sp = line.indexOf(' ', 4);
        if (sp < 0) { Serial.println("usage: sms <num> <text>"); return; }
        String num = line.substring(4, sp), text = line.substring(sp + 1);
        gsmCmd("AT+CMGF=1");
        String r = gsmCmd("AT+CMGS=\"" + num + "\"", 5000, ">");
        if (r.indexOf(">") < 0) { Serial.printf("[SMS] no prompt: %s\n", r.c_str()); return; }
        gsmSerial.print(text);
        gsmSerial.write(0x1A);
        String resp; uint32_t t0 = millis();
        while (millis() - t0 < 30000) {
            while (gsmSerial.available()) resp += (char)gsmSerial.read();
            if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) break;
        }
        resp.trim();
        Serial.printf("[SMS] -> %s\n", resp.c_str());
    } else if (line == "raw") {
        rawNmea = !rawNmea;
    } else if (line == "imu") {
        imuFast = !imuFast;
    } else if (line == "status") {
        printStatus();
    } else if (line.length()) {
        Serial.println("commands: at <cmd> | sms <num> <text> | raw | imu | status");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== NAZER bring-up ===");
    pinMode(STATUS_LED_PIN, OUTPUT);
    imuBringUp();
    gpsBringUp();
    gsmBringUp();
    Serial.printf("[BAT] %.2f V\n", batteryVolts());
    Serial.println("=== bring-up done, streaming ===");
}

void loop() {
    static uint32_t lastStatus = 0, lastImu = 0;
    static String cmdLine;

    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        gpsBytes++;
        gps.encode(c);
        if (rawNmea) Serial.write(c);
    }
    while (gsmSerial.available()) {   // unsolicited modem output (RING, +CMTI, etc.)
        String s = gsmSerial.readStringUntil('\n');
        s.trim();
        if (s.length()) Serial.printf("[GSM<] %s\n", s.c_str());
    }
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') { if (cmdLine.length()) handleCommand(cmdLine); cmdLine = ""; }
        else cmdLine += c;
    }
    if (imuFast && imuOk && millis() - lastImu > 100) { lastImu = millis(); printImu(); }
    if (millis() - lastStatus > 3000) {
        lastStatus = millis();
        digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
        printStatus();
    }
}
