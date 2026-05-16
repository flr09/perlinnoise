#include "Telemetry.h"
#include "../L0_platform/Sync.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L3_driver/Tmc2209.h"
#include "../L3_driver/Stepper.h"

namespace Telemetry {

struct LiveData {
    uint16_t sg = 0;
    uint8_t  cs = 0;
    int16_t  curA = 0, curB = 0;
    bool stall=false, otpw=false, ot=false, ola=false, olb=false;
};

static LiveData live[4];
static String csvBuffer;
static unsigned long startMs = 0;
static unsigned long lastRecordMs = 0;

static void pollTask(void*) {
    for (;;) {
        if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
                auto* d = Tmc::driver(i);
                if (!d) continue;
                live[i].sg = d->SG_RESULT();
                live[i].cs = d->cs_actual();
                uint32_t msc = d->MSCURACT();
                live[i].curA = (int16_t)(msc & 0x1FF);
                if (live[i].curA > 255) live[i].curA -= 512;
                live[i].curB = (int16_t)((msc >> 16) & 0x1FF);
                if (live[i].curB > 255) live[i].curB -= 512;
                uint32_t ds = d->DRV_STATUS();
                live[i].stall = (live[i].sg == 0);
                live[i].otpw  = d->otpw();
                live[i].ot    = d->ot();
                live[i].ola   = (ds >> 30) & 0x1;
                live[i].olb   = (ds >> 31) & 0x1;
            }
            xSemaphoreGive(Sync::uartMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void init() {
    resetBuffer();
    xTaskCreatePinnedToCore(pollTask, "TelemPoll", 3072, nullptr, 1, nullptr, 0);
}

void resetBuffer() {
    portENTER_CRITICAL(&Sync::motorMux);
    csvBuffer = "ts_ms,m,phase,val,pos,sps,rpm,pulses,sg,cs,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    startMs = millis();
    lastRecordMs = 0;
    portEXIT_CRITICAL(&Sync::motorMux);
}

void recordDataPoint(uint8_t motorIdx, const char* phase, float val) {
    if (motorIdx >= 4) return;
    unsigned long now = millis();
    if (csvBuffer.length() > MAX_BYTES || now - lastRecordMs < INTERVAL_MS) return;
    lastRecordMs = now;
    auto* s = Stepper::get(motorIdx);
    if (!s) return;

    char line[200];
    snprintf(line, sizeof(line),
        "%lu,%u,%s,%.0f,%ld,%d,%u,%lu,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - startMs, motorIdx, phase, val,
        s->getCurrentPosition(),
        (int)(s->getCurrentSpeedInMilliHz() / 1000),
        HalTacho::getRpm(motorIdx),
        HalTacho::getPulseCount(motorIdx),
        live[motorIdx].sg, live[motorIdx].cs,
        live[motorIdx].curA, live[motorIdx].curB,
        live[motorIdx].stall, live[motorIdx].otpw, live[motorIdx].ot,
        live[motorIdx].ola,  live[motorIdx].olb);

    portENTER_CRITICAL(&Sync::motorMux);
    csvBuffer += line;
    portEXIT_CRITICAL(&Sync::motorMux);
}

void recordEvent(uint8_t motorIdx, const char* phase, float val) {
    if (motorIdx >= 4) return;
    unsigned long now = millis();
    if (csvBuffer.length() > MAX_BYTES) return;
    auto* s = Stepper::get(motorIdx);
    if (!s) return;
    char line[200];
    snprintf(line, sizeof(line),
        "%lu,%u,%s,%.0f,%ld,%d,%u,%lu,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - startMs, motorIdx, phase, val,
        s->getCurrentPosition(),
        (int)(s->getCurrentSpeedInMilliHz() / 1000),
        HalTacho::getRpm(motorIdx),
        HalTacho::getPulseCount(motorIdx),
        live[motorIdx].sg, live[motorIdx].cs,
        live[motorIdx].curA, live[motorIdx].curB,
        live[motorIdx].stall, live[motorIdx].otpw, live[motorIdx].ot,
        live[motorIdx].ola, live[motorIdx].olb);
    portENTER_CRITICAL(&Sync::motorMux);
    csvBuffer += line;
    portEXIT_CRITICAL(&Sync::motorMux);
}

// Bug 56 (v4.4.11): Chunked Streaming statt 52-KB-Single-Alloc-Kopie.
// Vorher: `String c = csvBuffer;` unter Spinlock — Heap-Alloc + memcpy von
// bis zu 52 KB im Spinlock-CS (Memory #644, gleiches Pattern wie Bug 46
// bei Recorder). Jetzt: 256-Byte-Chunks unter kurzer CS, write außerhalb.
// totalLen wird einmal am Anfang snapshotted — neue Appends während des
// Streamings landen in der nächsten /telemetry-Anfrage. Append-Pfade
// (recordDataPoint/recordEvent) bleiben vorerst unverändert — geringere
// Frequenz (50 ms-Throttle) und kleinere Operationen als der Bulk-Copy.
void streamCsv(AsyncResponseStream* res) {
    if (!res) return;
    size_t totalLen;
    portENTER_CRITICAL(&Sync::motorMux);
    totalLen = csvBuffer.length();
    portEXIT_CRITICAL(&Sync::motorMux);

    char chunk[256];
    size_t offset = 0;
    while (offset < totalLen) {
        size_t copyLen;
        portENTER_CRITICAL(&Sync::motorMux);
        size_t curLen = csvBuffer.length();
        if (offset >= curLen) { portEXIT_CRITICAL(&Sync::motorMux); break; }
        copyLen = (totalLen - offset > sizeof(chunk)) ? sizeof(chunk) : (totalLen - offset);
        if (offset + copyLen > curLen) copyLen = curLen - offset;
        memcpy(chunk, csvBuffer.c_str() + offset, copyLen);
        portEXIT_CRITICAL(&Sync::motorMux);
        if (copyLen == 0) break;
        res->write((const uint8_t*)chunk, copyLen);
        offset += copyLen;
    }
}

void registerHandlers(AsyncWebServer& server) {
    server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest* r){
        AsyncResponseStream* res = r->beginResponseStream("text/csv");
        res->addHeader("Content-Disposition", "attachment; filename=\"parcour.csv\"");
        res->addHeader("Access-Control-Allow-Origin", "*");
        streamCsv(res);
        r->send(res);
    });
}

uint16_t getLatestSg(uint8_t i) { return i < 4 ? live[i].sg : 0; }
uint8_t  getLatestCs(uint8_t i) { return i < 4 ? live[i].cs : 0; }
bool     isStalled(uint8_t i)   { return i < 4 ? live[i].stall : false; }

} // namespace Telemetry
