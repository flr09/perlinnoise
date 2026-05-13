#include "Recorder.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Sync.h"

namespace Recorder {

static Sample        buf[CAPACITY];
static volatile size_t head     = 0;   // nächste Schreibposition
static volatile size_t filled   = 0;   // wieviele Samples gültig (max CAPACITY)
static volatile bool   recording = false;
static uint32_t        recStartMs = 0;
static uint32_t        lastSampleMs = 0;

static void resetBuffer() {
    portENTER_CRITICAL(&Sync::motorMux);
    head = 0;
    filled = 0;
    portEXIT_CRITICAL(&Sync::motorMux);
}

void start() {
    resetBuffer();
    recStartMs   = millis();
    lastSampleMs = 0;
    recording    = true;
    Logger::addLog("REC: started");
}

void stop() {
    if (!recording) return;
    recording = false;
    Logger::addLog(String("REC: stopped (") + (unsigned long)filled + " samples)");
}

bool isRecording()   { return recording; }
size_t count()       { return filled; }
uint32_t startedAtMs() { return recording ? recStartMs : 0; }

void tick(float flightX, float flightY, const float posDeg[4]) {
    if (!recording) return;
    uint32_t now = millis();
    if (now - lastSampleMs < INTERVAL_MS) return;
    lastSampleMs = now;

    Sample s;
    s.t_ms = now - recStartMs;
    for (uint8_t i = 0; i < 4; i++) s.posDeg[i] = posDeg ? posDeg[i] : 0.0f;
    s.flightX = flightX;
    s.flightY = flightY;

    portENTER_CRITICAL(&Sync::motorMux);
    buf[head] = s;
    head = (head + 1) % CAPACITY;
    if (filled < CAPACITY) filled++;
    portEXIT_CRITICAL(&Sync::motorMux);
}

String getCsv() {
    String out;
    out.reserve(80 + filled * 64);
    out = "t_ms,pos0,pos1,pos2,pos3,flightX,flightY\n";
    portENTER_CRITICAL(&Sync::motorMux);
    size_t n   = filled;
    size_t pos = (filled < CAPACITY) ? 0 : head;  // ältestes Sample
    for (size_t k = 0; k < n; k++) {
        const Sample& s = buf[(pos + k) % CAPACITY];
        char line[96];
        snprintf(line, sizeof(line),
                 "%lu,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f\n",
                 (unsigned long)s.t_ms,
                 s.posDeg[0], s.posDeg[1], s.posDeg[2], s.posDeg[3],
                 s.flightX, s.flightY);
        out += line;
    }
    portEXIT_CRITICAL(&Sync::motorMux);
    return out;
}

void registerHandlers(AsyncWebServer& server) {
    server.on("/rec/start", HTTP_GET, [](AsyncWebServerRequest* r) {
        start();
        r->send(200, "application/json",
                String("{\"recording\":true,\"started_ms\":") + (unsigned long)recStartMs + "}");
    });
    server.on("/rec/stop", HTTP_GET, [](AsyncWebServerRequest* r) {
        stop();
        r->send(200, "application/json",
                String("{\"recording\":false,\"samples\":") + (unsigned long)filled + "}");
    });
    server.on("/rec/state", HTTP_GET, [](AsyncWebServerRequest* r) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"recording\":%s,\"samples\":%u,\"capacity\":%u,\"interval_ms\":%lu}",
                 recording ? "true" : "false",
                 (unsigned)filled, (unsigned)CAPACITY, (unsigned long)INTERVAL_MS);
        r->send(200, "application/json", buf);
    });
    server.on("/rec/csv", HTTP_GET, [](AsyncWebServerRequest* r) {
        String csv = getCsv();
        AsyncWebServerResponse* res = r->beginResponse(200, "text/csv", csv);
        res->addHeader("Content-Disposition", "attachment; filename=\"recorder.csv\"");
        res->addHeader("Access-Control-Allow-Origin", "*");
        r->send(res);
    });
}

} // namespace Recorder
