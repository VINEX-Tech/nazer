#include "Cellular.h"
#include "GSM.h"
#include "config.h"
#include "LocalStorage.h"
#include "Storage.h"
#include "PointRing.h"
#include <esp_system.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define CELL(fmt, ...) Serial.printf("[CELL] " fmt "\n", ##__VA_ARGS__)

namespace {

enum class JobType : uint8_t { SMS, EVENT, VIOLATION };

struct Job {
    JobType type;
    uint8_t attempts;
    char    to[12];       // SMS only
    char    body[400];
};

constexpr uint8_t  MAX_ATTEMPTS        = 20;
constexpr uint32_t SMS_POLL_MS         = 15000;
constexpr uint32_t STATUS_POLL_MS      = 30000;
constexpr uint32_t RETRY_BACKOFF_MS    = 20000;

GSM               g_gsm;
TaskHandle_t      g_task      = nullptr;
QueueHandle_t     g_urgent    = nullptr;   // SMS + events
QueueHandle_t     g_normal    = nullptr;   // violations
QueueHandle_t     g_inbound   = nullptr;   // received SMS
SemaphoreHandle_t g_lock      = nullptr;   // guards point FIFO + status

constexpr uint16_t RAM_POINTS = 120;
constexpr uint8_t BATCH_POINTS = 10; // bounded payload/heap on ESP32; contract allows up to 50
PointRing<String,RAM_POINTS> g_points;
LocalStorage g_store;
bool g_uploadRequested=false, g_parked=false, g_batchInFlight=false;
uint32_t g_batchRetryMs=0;

// Caller holds g_lock. Keep oldest records in NVS and newer records in RAM.
void spill() {
    while (g_points.size() && g_store.enqueueTelemetry(g_points.at(0).c_str())) {
        g_points.ack(1);
    }
}
CellularStatus g_status = {false, false, false, 99, 0, 0};
uint32_t       g_retryAfterMs = 0;

void setStatus(void (*fn)(CellularStatus&)) {
    xSemaphoreTake(g_lock, portMAX_DELAY);
    fn(g_status);
    xSemaphoreGive(g_lock);
}

bool modemInit() {
    if (!g_gsm.begin()) return false;
    g_gsm.sendAT("ATE0", "OK", 1000);
    g_gsm.sendAT("AT+CMGF=1", "OK", 2000);
    // Store incoming SMS on the SIM without unsolicited +CMT output; we poll.
    g_gsm.sendAT("AT+CNMI=2,1,0,0,0", "OK", 2000);
    return true;
}

void refreshStatus() {
    int csq = g_gsm.signalQuality();
    char buf[64];
    bool reg = false;
    if (g_gsm.sendATCapture("AT+CREG?", buf, sizeof(buf), 2000)) {
        reg = strstr(buf, ",1") || strstr(buf, ",5");   // home or roaming
    }
    static int s_csq; static bool s_reg;
    s_csq = csq; s_reg = reg;
    setStatus([](CellularStatus& s) {
        s.csq = (uint8_t)s_csq;
        s.registered = s_reg;
        s.gprsUp = g_gsm.isConnected();
    });
}

int httpPost(const char* path, const char* body, char* resp, size_t respLen) {
    if (!g_gsm.isConnected() && !g_gsm.gprsConnect(GSM_APN)) return -10;
    char url[160];
    snprintf(url, sizeof(url), "%s%s", SERVER_HOST, path);
    g_gsm.sendAT("AT+HTTPTERM", "OK", 1000);   // clear a session left open by a failed call
    int code = g_gsm.httpPost(url, body, resp, respLen, "X-Device-Key: " DEVICE_SECRET_KEY);
    if (code != 200) {
        CELL("POST %s -> %d", path, code);
        // Anything other than a server answer means the bearer is suspect.
        if (code < 100) g_gsm.gprsDisconnect();
    }
    return code;
}


bool runJob(Job& j) {
    char resp[256];
    switch (j.type) {
    case JobType::SMS:
        return g_gsm.sendSms(j.to, j.body);
    case JobType::EVENT:
        return httpPost(SERVER_PATH_EVENT, j.body, resp, sizeof(resp)) == 200;
    case JobType::VIOLATION:
        return httpPost(SERVER_PATH_VIOLATION, j.body, resp, sizeof(resp)) == 200;
    }
    return false;
}

bool uploadRequested() {
    xSemaphoreTake(g_lock,portMAX_DELAY);
    bool value=g_uploadRequested;
    xSemaphoreGive(g_lock); return value;
}
bool parked() {
    xSemaphoreTake(g_lock,portMAX_DELAY);
    bool value=g_parked;
    xSemaphoreGive(g_lock); return value;
}
void sendTelemetryIfAny() {
    if (int32_t(millis()-g_batchRetryMs)<0) return;
    String body="{\"device_id\":\""+Storage.getDeviceId()+"\",\"points\":[";
    if (!body.reserve(BATCH_POINTS*896+128)) {
        g_batchRetryMs=millis()+RETRY_BACKOFF_MS; return;
    }
    size_t expectedLength=body.length()+2; // closing array and object
    uint8_t persisted=0, ram=0;
    char point[896];
    xSemaphoreTake(g_lock,portMAX_DELAY);
    uint8_t available=g_store.telemetryCount();
    for (; persisted<available && persisted<BATCH_POINTS; ++persisted) {
        if (!g_store.peekTelemetry(persisted,point,sizeof(point))) break;
        if (persisted) body+=',';
        body+=point;
        expectedLength+=strlen(point)+(persisted ? 1 : 0);
    }
    // Never bypass an unreadable persistent record.
    if (persisted==available) {
        for (; ram<g_points.size() && persisted+ram<BATCH_POINTS; ++ram) {
            if (persisted+ram) body+=',';
            body+=g_points.at(ram);
            expectedLength+=g_points.at(ram).length()+(persisted+ram ? 1 : 0);
        }
    }
    body+="]}";
    if (body.length()!=expectedLength) {
        xSemaphoreGive(g_lock);
        g_batchRetryMs=millis()+RETRY_BACKOFF_MS; return;
    }
    g_batchInFlight=persisted || ram;
    xSemaphoreGive(g_lock);
    if (!persisted && !ram) {
        xSemaphoreTake(g_lock,portMAX_DELAY); g_uploadRequested=false; xSemaphoreGive(g_lock); return;
    }
    char resp[256];
    int code=httpPost("/v1/device/telemetry/batch",body.c_str(),resp,sizeof(resp));
    if (code<200 || code>=300) {
        xSemaphoreTake(g_lock,portMAX_DELAY); g_batchInFlight=false; xSemaphoreGive(g_lock);
        g_batchRetryMs=millis()+RETRY_BACKOFF_MS; return;
    }
    JsonDocument doc;
    int limit=deserializeJson(doc,resp)==DeserializationError::Ok ? (doc["speed_limit_kmh"] | 0) : 0;
    xSemaphoreTake(g_lock,portMAX_DELAY);
    g_batchInFlight=false;
    g_store.ackTelemetry(persisted);
    g_points.ack(ram);
    g_status.lastPostOkMs=millis(); g_status.serverLimitKmh=limit; g_status.gprsUp=true;
    g_uploadRequested=g_points.size() || g_store.telemetryCount();
    xSemaphoreGive(g_lock);
}

void pollInbox() {
    InboundSms in;
    for (int i = 0; i < 5; i++) {   // drain a few per cycle
        if (!g_gsm.popSms(in.from, sizeof(in.from), in.text, sizeof(in.text))) break;
        CELL("SMS from %s: %s", in.from, in.text);
        xQueueSend(g_inbound, &in, 0);
    }
}

void requeueOrDrop(QueueHandle_t q, Job& j) {
    if (++j.attempts >= MAX_ATTEMPTS) {
        CELL("dropping job type=%d after %u attempts", (int)j.type, j.attempts);
        return;
    }
    g_retryAfterMs = millis() + RETRY_BACKOFF_MS;
    xQueueSendToFront(q, &j, 0);
}

void task(void*) {
    while (!modemInit()) {
        CELL("modem not responding, retrying in 10 s");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    setStatus([](CellularStatus& s) { s.modemOk = true; });
    refreshStatus();
    CELL("ready (csq=%u)", g_status.csq);

    uint32_t lastSmsPoll = 0, lastStatus = millis();
    static Job job;   // large; keep it off the task stack
    bool sleeping=false;
    for (;;) {
        uint32_t now = millis();
        xSemaphoreTake(g_lock,portMAX_DELAY);
        if (g_points.size()>RAM_POINTS*80/100) spill();
        xSemaphoreGive(g_lock);
        bool canRetry = (int32_t)(now - g_retryAfterMs) >= 0;
        bool batchReady=uploadRequested() && int32_t(now-g_batchRetryMs)>=0;
        bool work=(canRetry && (uxQueueMessagesWaiting(g_urgent) || uxQueueMessagesWaiting(g_normal))) || batchReady;
        bool needsAwake=!parked() || work || now-lastSmsPoll>=SMS_POLL_MS;
        if (sleeping && needsAwake) {
            if (!g_gsm.wake()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            sleeping=false;
        }
        if (!needsAwake) {
            if (!sleeping) {
                sleeping=g_gsm.sleep();
                setStatus([](CellularStatus& s) { s.gprsUp=false; });
            }
            vTaskDelay(pdMS_TO_TICKS(200)); continue;
        }
        if (canRetry && xQueueReceive(g_urgent, &job, 0) == pdTRUE) {
            if (!runJob(job)) requeueOrDrop(g_urgent, job);
        } else if (now - lastSmsPoll >= SMS_POLL_MS) {
            lastSmsPoll = now;
            pollInbox();
        } else if (canRetry && xQueueReceive(g_normal, &job, 0) == pdTRUE) {
            if (!runJob(job)) requeueOrDrop(g_normal, job);
        } else if (batchReady) {
            sendTelemetryIfAny();
        } else if (now - lastStatus >= STATUS_POLL_MS) {
            lastStatus = now;
            refreshStatus();
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

bool enqueue(QueueHandle_t q, JobType type, const char* to, const char* body) {
    if (!q || strlen(body) >= sizeof(Job::body)) return false;
    static Job j;   // callers are all on the main loop task
    j.type = type;
    j.attempts = 0;
    strncpy(j.to, to ? to : "", sizeof(j.to) - 1);
    j.to[sizeof(j.to) - 1] = '\0';
    strncpy(j.body, body, sizeof(j.body) - 1);
    j.body[sizeof(j.body) - 1] = '\0';
    return xQueueSend(q, &j, 0) == pdTRUE;
}

}  // namespace

namespace Cellular {

void begin() {
    g_lock    = xSemaphoreCreateMutex();
    g_store.begin();
    g_uploadRequested=g_store.telemetryCount()>0;
    esp_register_shutdown_handler(persistPending);
    g_urgent  = xQueueCreate(8, sizeof(Job));
    g_normal  = xQueueCreate(8, sizeof(Job));
    g_inbound = xQueueCreate(4, sizeof(InboundSms));
    xTaskCreatePinnedToCore(task, "cellular", 8192, nullptr, 1, &g_task, 0);
}

bool postTelemetry(const char* json) {
    if (!g_lock || !json || !json[0] || strlen(json)>=896) return false;
    xSemaphoreTake(g_lock,portMAX_DELAY);
    String point(json);
    bool ok=point.length()==strlen(json) && g_points.push(point);
    // Also spill while the modem is still stuck in its initial AT handshake.
    if (g_points.size()>RAM_POINTS*80/100 && !g_batchInFlight) spill();
    xSemaphoreGive(g_lock);
    if (!ok) CELL("telemetry capacity exhausted; keeping unacknowledged points");
    return ok;
}
void requestUpload() {
    if (!g_lock) return;
    xSemaphoreTake(g_lock,portMAX_DELAY); g_uploadRequested=true; xSemaphoreGive(g_lock);
}
void setParked(bool value) {
    xSemaphoreTake(g_lock,portMAX_DELAY); g_parked=value; xSemaphoreGive(g_lock);
}
uint16_t queued() {
    if (!g_lock) return 0;
    xSemaphoreTake(g_lock,portMAX_DELAY);
    uint16_t n=g_points.size()+g_store.telemetryCount();
    xSemaphoreGive(g_lock); return n;
}
void persistPending() {
    if (!g_lock) return;
    if (xSemaphoreTake(g_lock,pdMS_TO_TICKS(1000))==pdTRUE) {
        // Shutdown is final: stop the worker before moving an in-flight RAM head.
        if (g_task && g_task!=xTaskGetCurrentTaskHandle()) vTaskSuspend(g_task);
        spill();
        if (g_points.size()) CELL("NVS full: %u RAM points cannot survive reboot",(unsigned)g_points.size());
        xSemaphoreGive(g_lock);
    }
}

bool postViolation(const char* json) { return enqueue(g_normal, JobType::VIOLATION, nullptr, json); }
bool postEvent(const char* json)     { return enqueue(g_urgent, JobType::EVENT, nullptr, json); }
bool sendSms(const char* number, const char* text) { return enqueue(g_urgent, JobType::SMS, number, text); }

bool popInboundSms(InboundSms& out) {
    return g_inbound && xQueueReceive(g_inbound, &out, 0) == pdTRUE;
}

CellularStatus status() {
    CellularStatus s = {};
    if (!g_lock) return s;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    s = g_status;
    xSemaphoreGive(g_lock);
    return s;
}

}  // namespace Cellular
