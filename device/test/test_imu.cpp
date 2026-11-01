// ============================================================
// test_imu.cpp: Phase 3 IMU Test Sketch
// Rewritten to use SH1106Wire (ThingPulse) instead of U8g2
// to match the library already in platformio.ini.
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <SH1106Wire.h>
#include "config.h"
#include <IMU.h>

// SH1106 128×64, hardware I2C
static SH1106Wire oled(OLED_I2C_ADDR, SDA_PIN, SCL_PIN);

static IMU imu;

static bool      buttonPressed = false;
static uint32_t  lastDisplayMs = 0;
static uint32_t  lastCsvMs     = 0;
static uint8_t   calProgress   = 0;
static uint8_t   currentScreen = 0;  // 0 = data, 1 = tilt

static const uint32_t DISPLAY_MS = 100;
static const uint32_t CSV_MS     = 100;

// ---- Encoder button ISR ----
static volatile uint32_t lastBtnMs = 0;

void IRAM_ATTR onButtonPress() {
    uint32_t now = millis();
    if (now - lastBtnMs > 50) {
        lastBtnMs     = now;
        buttonPressed = true;
    }
}

// ---- Calibration progress callback ----
void onCalProgress(uint8_t pct) {
    calProgress = pct;

    oled.clear();
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    oled.drawString(0, 0, "CALIBRATING");
    oled.drawString(0, 14, "Keep flat & still...");

    // Progress bar: x=4, y=30, w=120, h=10
    oled.drawRect(4, 30, 120, 10);
    int fillW = (int)((pct * 116UL) / 100);
    if (fillW > 0) oled.fillRect(5, 31, fillW, 8);

    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.drawString(64, 46, pctStr);

    oled.display();
}

// ---- Data screen ----
void drawDataScreen(const IMUData& d) {
    oled.clear();
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);

    // Line 1: calibration state + raw magnitude
    char line1[28];
    snprintf(line1, sizeof(line1), "%s  |%.2f| m/s2",
        d.calibrated ? "CAL" : "RAW", d.rawMagnitude);
    oled.drawString(0, 0, line1);

    // Lines 2–4: calibrated accel per axis
    char buf[24];
    snprintf(buf, sizeof(buf), "X: %+.3f m/s2", d.accelX);
    oled.drawString(0, 13, buf);
    snprintf(buf, sizeof(buf), "Y: %+.3f m/s2", d.accelY);
    oled.drawString(0, 26, buf);
    snprintf(buf, sizeof(buf), "Z: %+.3f m/s2", d.accelZ);
    oled.drawString(0, 39, buf);

    // Line 5: tilt angles
    snprintf(buf, sizeof(buf), "P:%+.1f R:%+.1f deg", d.tiltX, d.tiltY);
    oled.drawString(0, 52, buf);

    // Hint if not calibrated
    if (!d.calibrated) {
        oled.setTextAlignment(TEXT_ALIGN_RIGHT);
        oled.drawString(127, 0, "BTN=CAL");
    }

    oled.display();
}

// ---- Tilt dot screen ----
void drawTiltScreen(const IMUData& d) {
    oled.clear();

    // Bounding circle and crosshair (left 64px area)
    const int cx = 48, cy = 32, r = 26;
    oled.drawCircle(cx, cy, r);
    // Horizontal crosshair
    for (int x = cx - r; x <= cx + r; x++) oled.setPixel(x, cy);
    // Vertical crosshair
    for (int y = cy - r; y <= cy + r; y++) oled.setPixel(cx, y);

    // Map tilt to pixel offset (clamp ±30°)
    float pitch = constrain(d.tiltX, -30.0f, 30.0f);
    float roll  = constrain(d.tiltY, -30.0f, 30.0f);
    int dotX = cx + (int)(roll  * r / 30.0f);
    int dotY = cy - (int)(pitch * r / 30.0f);

    // 5×5 filled dot
    oled.fillRect(dotX - 2, dotY - 2, 5, 5);

    // Right panel: text readout
    oled.setFont(ArialMT_Plain_10);
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    char buf[14];
    snprintf(buf, sizeof(buf), "P:%+.1f", d.tiltX);
    oled.drawString(100, 20, buf);
    snprintf(buf, sizeof(buf), "R:%+.1f", d.tiltY);
    oled.drawString(100, 34, buf);
    oled.drawString(88, 52, "TILT");

    oled.display();
}

// ============================================================
// setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) {}

    Serial.println("\n[I][TEST] NAZER Phase 3: IMU Test");
    Serial.println("[I][TEST] ================================");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);

    // OLED init
    oled.init();
    oled.flipScreenVertically();
    oled.setFont(ArialMT_Plain_10);
    oled.clear();
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
    oled.drawString(0, 0, "NAZER Phase 3");
    oled.drawString(0, 16, "IMU Init...");
    oled.display();
    Serial.println("[I][TEST] OLED OK");

    // Encoder button
    pinMode(ENC_BTN_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(ENC_BTN_PIN), onButtonPress, FALLING);

    // IMU
    imu.setCalibrationProgressCallback(onCalProgress);

    if (!imu.begin()) {
        oled.clear();
        oled.drawString(0, 0,  "IMU NOT FOUND");
        oled.drawString(0, 16, "Check wiring!");
        oled.drawString(0, 32, "SDA=21 SCL=22");
        oled.drawString(0, 48, "Run I2C scanner");
        oled.display();

        Serial.println("[E][TEST] IMU init failed. Halting.");
        Serial.println("[E][TEST]   1. Flash the I2C scanner sketch");
        Serial.println("[E][TEST]   2. Expect 0x3C (OLED) and 0x68 (MPU)");
        Serial.println("[E][TEST]   3. Check GY-521 VCC=3.3V, SDA=21, SCL=22");
        while (true) delay(1000);
    }

    IMU::printCsvHeader();
    Serial.println("[I][TEST] Press encoder button to calibrate");
    Serial.println("[I][TEST] After calibration: button toggles data/tilt screens");

    oled.clear();
    oled.drawString(0, 0,  "IMU OK");
    oled.drawString(0, 16, imu.isCalibrated() ? "Cal: LOADED" : "Cal: NONE");
    oled.drawString(0, 32, "BTN = Calibrate");
    oled.display();
    delay(1500);
}

// ============================================================
// loop()
// ============================================================
void loop() {
    uint32_t now = millis();

    // Handle button
    if (buttonPressed) {
        buttonPressed = false;
        if (!imu.isCalibrated()) {
            Serial.println("[I][TEST] Button pressed: starting calibration");
            imu.calibrate();
            calProgress = 0;
        } else {
            currentScreen = (currentScreen + 1) % 2;
            Serial.printf("[I][TEST] Screen -> %s\n",
                currentScreen == 0 ? "DATA" : "TILT");
        }
    }

    imu.update();

    // Update display at 10Hz
    if (now - lastDisplayMs >= DISPLAY_MS) {
        lastDisplayMs = now;
        IMUData d = imu.getData();
        if (currentScreen == 0) drawDataScreen(d);
        else                    drawTiltScreen(d);
    }

    // Stream CSV at 10Hz
    if (now - lastCsvMs >= CSV_MS) {
        lastCsvMs = now;
        imu.printCsvRow();
    }
}