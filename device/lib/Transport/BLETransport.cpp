// =============================================================
// BLETransport.cpp: Implementation
// =============================================================

#include "BLETransport.h"
#include "Storage.h"
#include <esp_random.h>
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>  // esp_efuse_mac_get_default on newer ESP-IDF
#endif

// ---- Release builds must not ship the passkey placeholder ----
#if defined(NAZER_RELEASE) && !NAZER_HAS_DISPLAY && ((BLE_PASSKEY) < 1 || (BLE_PASSKEY) > 999999)
#error "Release build: set BLE_PASSKEY (1..999999) in include/config.local.h"
#endif

// ============================================================
// Bonding policy: window for new bonds, passkey per pairing.
// ============================================================
static blepair::Window g_pairingWindow;
static void (*g_passkeyShow)(uint32_t) = nullptr;

class NazerSecurityCallbacks : public BLESecurityCallbacks {
public:
    // A central asks to pair. Refuse unless the bonding window is open.
    bool onSecurityRequest() override {
        if (!g_pairingWindow.isOpen(millis())) {
            Serial.println("[W][BLE] Pairing refused: bonding window closed");
            return false;
        }
#if NAZER_HAS_DISPLAY
        uint32_t r = 0;
        esp_fill_random(&r, sizeof(r));
        uint32_t key = blepair::passkeyFromRandom(r);
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &key, sizeof(key));
        if (g_passkeyShow) g_passkeyShow(key);
#endif
        return true;
    }
    // The stack reports the passkey it uses: show it where the user can read it.
    void onPassKeyNotify(uint32_t key) override {
#if NAZER_HAS_DISPLAY
        Serial.printf("[I][BLE] Pairing passkey: %06lu\n", (unsigned long)key);
        if (g_passkeyShow) g_passkeyShow(key);
#else
        (void)key;   // static passkey: never printed
#endif
    }
    uint32_t onPassKeyRequest() override { return 0; }      // this device has no keypad
    bool onConfirmPIN(uint32_t) override { return false; }  // numeric comparison not offered
    void onAuthenticationComplete(esp_ble_auth_cmpl_t r) override {
        Serial.printf("[I][BLE] Bonding %s\n", r.success ? "complete" : "failed");
    }
};
static NazerSecurityCallbacks g_securityCallbacks;

void BLETransport::setPasskeyDisplay(void (*show)(uint32_t)) { g_passkeyShow = show; }
void BLETransport::openPairingWindow() {
    g_pairingWindow.open(millis());
    Serial.printf("[I][BLE] Accepting new bonds for %lu s\n", (unsigned long)(blepair::WINDOW_MS / 1000));
}
bool BLETransport::pairingWindowOpen() { return g_pairingWindow.isOpen(millis()); }

// ============================================================
// NazerCommandCallbacks::onWrite()
// Called when the phone writes to the command characteristic.
// Payloads: see contracts.md "v2 additions" (set_limit, set_mode, ...)
// ============================================================
// Runs on the BLE stack task: only copy the bytes out, main.cpp parses them.
void NazerCommandCallbacks::onWrite(BLECharacteristic* c) {
    String v = c->getValue().c_str();
    if (v.length() == 0 || !queue) return;
    char buf[BLE_CMD_MAX] = {};
    strncpy(buf, v.c_str(), sizeof(buf) - 1);
    if (xQueueSend(queue, buf, 0) != pdTRUE) {
        Serial.println("[W][BLE] Command queue full, dropped");
    }
}

// ============================================================
// _makeDeviceName()
// ============================================================
String BLETransport::_makeDeviceName() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[12];
    snprintf(buf, sizeof(buf), "NAZER-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

// ============================================================
BLETransport::BLETransport()
    : _failures(0)
    , _lastNotifyMs(0)
    , _hasPendingTelemetry(false)
{
    _pendingTelemetry[0] = '\0';
    _deviceName = _makeDeviceName();
}

// ============================================================
BLETransport::~BLETransport() {
    disconnect();
}

// ============================================================
bool BLETransport::connect() {
    Serial.printf("[I][BLE] Initialising BLE as \"%s\"\n", _deviceName.c_str());

#if !NAZER_HAS_DISPLAY
    // No screen to show a random passkey: the static BLE_PASSKEY is the
    // fallback. Fail closed, like BleAuth: no valid passkey, no BLE at all.
    if (!blepair::validStaticPasskey((long long)BLE_PASSKEY)) {
        Serial.println("[E][BLE] Device unconfigured: BLE_PASSKEY must be 1..999999. "
                       "BLE security and advertising NOT started.");
        return false;
    }
#endif

    BLEDevice::init(_deviceName.c_str());

    // ---- Link security: LE Secure Connections, MITM protection, bonding ----
    // The phone must bond with a passkey (Android shows a prompt) before it
    // can subscribe or write: every characteristic below needs an encrypted,
    // authenticated link, so the pairing code and the claim answer never
    // cross the air in clear. New bonds are only accepted while the pairing
    // window is open (BlePairingPolicy.h); NazerSecurityCallbacks enforces it.
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(&g_securityCallbacks);
    BLESecurity* security = new BLESecurity();
#if NAZER_HAS_DISPLAY
    // A fresh random passkey is set for each pairing request (see
    // NazerSecurityCallbacks::onSecurityRequest) and shown on the OLED.
    uint32_t first = 0;
    esp_fill_random(&first, sizeof(first));
    security->setStaticPIN(blepair::passkeyFromRandom(first));
#else
    security->setStaticPIN(BLE_PASSKEY);                       // documented fallback, no display
#endif
    security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    security->setCapability(ESP_IO_CAP_OUT);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    uint8_t onlySpecified = ESP_BLE_ONLY_ACCEPT_SPECIFIED_AUTH_ENABLE;   // refuse legacy / no-MITM pairing
    esp_ble_gap_set_security_param(ESP_BLE_SM_ONLY_ACCEPT_SPECIFIED_SEC_AUTH, &onlySpecified, sizeof(onlySpecified));
    g_pairingWindow.open(millis());
    Serial.printf("[I][BLE] Accepting new bonds for %lu s\n", (unsigned long)(blepair::WINDOW_MS / 1000));

    // Request 247-byte MTU so chunks are not truncated on early notifications.
    // The phone will negotiate down if it can't support 247, but most Android
    // and iOS devices support 247 or higher. Must be called before createServer().
    BLEDevice::setMTU(247);

    // ---- Create server ----
    _server = BLEDevice::createServer();
    _server->setCallbacks(&_serverCallbacks);

    // ---- Create service ----
    BLEService* service = _server->createService(BLE_SERVICE_UUID);

    // ---- Telemetry characteristic (NOTIFY) ----
    _telemetryChar = service->createCharacteristic(
        BLE_CHAR_TELEMETRY,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    // BLE2902 is the CCCD: required for NOTIFY to work.
    // Without it, nRF Connect shows the characteristic but can't enable notifications.
    _telemetryChar->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
    BLE2902* cccd = new BLE2902();
    cccd->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM);
    _telemetryChar->addDescriptor(cccd);

    // ---- Command characteristic (WRITE) ----
    _commandChar = service->createCharacteristic(
        BLE_CHAR_COMMAND,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _commandChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
    _cmdCallbacks.queue = xQueueCreate(6, BLE_CMD_MAX);
    _commandChar->setCallbacks(&_cmdCallbacks);

    // ---- Start service ----
    service->start();

    // ---- Start advertising ----
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[I][BLE] Advertising as \"%s\": connect with nRF Connect\n",
                  _deviceName.c_str());
    Serial.printf("[I][BLE] Service UUID:   %s\n", BLE_SERVICE_UUID);
    Serial.printf("[I][BLE] Telemetry UUID: %s  (enable NOTIFY)\n", BLE_CHAR_TELEMETRY);
    Serial.printf("[I][BLE] Command UUID:   %s  (WRITE JSON cmd)\n", BLE_CHAR_COMMAND);

    _ready = true;
    return true;
}

// ============================================================
void BLETransport::disconnect() {
    if (_server) {
        BLEDevice::stopAdvertising();
        Serial.println("[I][BLE] Advertising stopped");
    }
}

// ============================================================
bool BLETransport::isConnected() {
    return _serverCallbacks.clientConnected;
}

// ============================================================
bool BLETransport::sendTelemetry(const char* jsonBody) {
    strncpy(_pendingTelemetry, jsonBody, sizeof(_pendingTelemetry) - 1);
    _pendingTelemetry[sizeof(_pendingTelemetry) - 1] = '\0';
    _hasPendingTelemetry = true;

    if (authorized()) {
        _notifyChunked(_telemetryChar, _pendingTelemetry);
        _hasPendingTelemetry = false;
        return true;
    }

    Serial.println("[D][BLE] Telemetry queued (no client connected)");
    _failures = (_failures < 255) ? _failures + 1 : 255;
    return false;
}

// ============================================================
bool BLETransport::sendViolation(const char* jsonBody) {
    if (!authorized()) {
        Serial.println("[W][BLE] Violation not sent: no BLE client connected");
        _failures = (_failures < 255) ? _failures + 1 : 255;
        return false;
    }
    _notifyChunked(_telemetryChar, jsonBody);
    _failures = 0;
    return true;
}

// ============================================================
void BLETransport::tick(const char* latestTelemetryJson) {
    if (!authorized()) return;

    uint32_t now = millis();
    if (now - _lastNotifyMs < BLE_TELEMETRY_INTERVAL_MS) return;
    _lastNotifyMs = now;

    const char* toSend = _hasPendingTelemetry ? _pendingTelemetry : latestTelemetryJson;
    if (!toSend || toSend[0] == '\0') return;

    _notifyChunked(_telemetryChar, toSend);
    _hasPendingTelemetry = false;
    _failures = 0;

    Serial.printf("[D][BLE] Telemetry notified (%u bytes)\n", (unsigned)strlen(toSend));
}

// ============================================================
// _notifyChunked()
// Breaks JSON into BLE_CHUNK_SIZE-byte fragments, each prefixed
// "N/M|" (e.g. "1/2|..." / "2/2|...") for phone-side reassembly.
// ============================================================
void BLETransport::_notifyChunked(BLECharacteristic* c, const char* json) {
    size_t totalLen = strlen(json);
    if (totalLen == 0) return;

    uint8_t totalChunks = (uint8_t)((totalLen + BLE_CHUNK_SIZE - 1) / BLE_CHUNK_SIZE);

    for (uint8_t i = 0; i < totalChunks; i++) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%d/%d|", i + 1, totalChunks);
        size_t prefixLen = strlen(prefix);

        // prefix(7) + data(200) + null(1) = 208: buffer is 209
        char chunk[BLE_CHUNK_SIZE + 9];
        strncpy(chunk, prefix, prefixLen);
        size_t offset    = i * BLE_CHUNK_SIZE;
        size_t chunkData = min((size_t)BLE_CHUNK_SIZE, totalLen - offset);
        memcpy(chunk + prefixLen, json + offset, chunkData);
        chunk[prefixLen + chunkData] = '\0';

        c->setValue((uint8_t*)chunk, prefixLen + chunkData);
        c->notify();

        if (i < totalChunks - 1) delay(20);
    }
}
void BLETransport::notifyJson(const char* json) {
    if (!authorized() || !_telemetryChar) return;
    _notifyChunked(_telemetryChar, json);
}

void BLETransport::notifyAuthStatus(const char* json) {
    if (!_serverCallbacks.clientConnected || !_telemetryChar) return;
    _notifyChunked(_telemetryChar, json);
}

bool BLETransport::popCommand(char* out, size_t len) {
    if (!_cmdCallbacks.queue || len < BLE_CMD_MAX) return false;
    return xQueueReceive(_cmdCallbacks.queue, out, 0) == pdTRUE;
}

void BLETransport::setParked(bool parked) {
    if (!_ready) return;   // BLE never started (unconfigured passkey)
    BLEDevice::stopAdvertising();
    auto adv=BLEDevice::getAdvertising();
    adv->setMinInterval(parked ? 1600 : 160);
    adv->setMaxInterval(parked ? 1680 : 240);
    if (!clientConnected()) BLEDevice::startAdvertising();
}
