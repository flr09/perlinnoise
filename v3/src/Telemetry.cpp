#include "Telemetry.h"
#include "Driver.h"
#include "Sensor.h"

extern portMUX_TYPE motorMux;
extern SemaphoreHandle_t uartMutex;

struct TelemCache {
    uint16_t sg   = 0;
    uint8_t  cs   = 0;
    int16_t  cur_a = 0, cur_b = 0;
    bool stall = false, otpw = false, ot = false, ola = false, olb = false;
} tCache;

String        telemCSV  = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    portENTER_CRITICAL(&motorMux);
    telemCSV   = "ts_ms,phase,val,pos_steps,spd_sps,real_rpm,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStart = millis();
    lastTelemMs = 0;
    portEXIT_CRITICAL(&motorMux);
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES || now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;
    if (!stepper) return;
    char line[140];
    snprintf(line, sizeof(line),
        "%lu,%s,%.0f,%ld,%d,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val,
        stepper->getCurrentPosition(),
        (int)(stepper->getCurrentSpeedInMilliHz() / 1000),
        getTachoRpm(),
        tCache.sg, tCache.cs, tCache.cur_a, tCache.cur_b,
        tCache.stall, tCache.otpw, tCache.ot, tCache.ola, tCache.olb);
    
    portENTER_CRITICAL(&motorMux);
    telemCSV += line;
    portEXIT_CRITICAL(&motorMux);
}

uint16_t telemCacheSG() { return tCache.sg; }
uint8_t  telemCacheCS() { return tCache.cs; }

void updateTelemCache() {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        tCache.sg = driverX.SG_RESULT();
        tCache.cs = driverX.cs_actual();
        uint32_t msc = driverX.MSCURACT();
        tCache.cur_a = (int16_t)(msc & 0x1FF);
        if (tCache.cur_a > 255) tCache.cur_a -= 512;
        tCache.cur_b = (int16_t)((msc >> 16) & 0x1FF);
        if (tCache.cur_b > 255) tCache.cur_b -= 512;
        uint32_t ds = driverX.DRV_STATUS();
        tCache.stall = (tCache.sg == 0);  // TMC2209: SG_RESULT==0 = Stall; (ds & 0x1) war LSB von SG_RESULT, kein Stall-Bit
        tCache.otpw  = driverX.otpw();
        tCache.ot    = driverX.ot();
        tCache.ola   = (ds >> 30) & 0x1;
        tCache.olb   = (ds >> 31) & 0x1;
        xSemaphoreGive(uartMutex);
    }
}
