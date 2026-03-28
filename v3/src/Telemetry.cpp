#include "Telemetry.h"
#include "Driver.h"
#include "Sensor.h"

// System-Muxer aus MotorControl.cpp
extern portMUX_TYPE motorMux;
extern SemaphoreHandle_t uartMutex;
extern FastAccelStepper* stepper;

struct RawHardwareData {
    uint16_t sg   = 0;
    uint8_t  cs   = 0;
    int16_t  cur_a = 0, cur_b = 0;
    bool stall = false, otpw = false, ot = false, ola = false, olb = false;
} liveData;

static String        csvBuffer  = "";
static unsigned long telemStartTime = 0;
static unsigned long lastRecordMs   = 0;

// Hintergrund-Task: Poll TMC-Register (Core 0)
void TelemetryPollTask(void* pvParameters) {
    for (;;) {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            liveData.sg = driverX.SG_RESULT();
            liveData.cs = driverX.cs_actual();
            
            uint32_t msc = driverX.MSCURACT();
            liveData.cur_a = (int16_t)(msc & 0x1FF);
            if (liveData.cur_a > 255) liveData.cur_a -= 512;
            liveData.cur_b = (int16_t)((msc >> 16) & 0x1FF);
            if (liveData.cur_b > 255) liveData.cur_b -= 512;
            
            uint32_t ds = driverX.DRV_STATUS();
            liveData.stall = (liveData.sg == 0);
            liveData.otpw  = driverX.otpw();
            liveData.ot    = driverX.ot();
            liveData.ola   = (ds >> 30) & 0x1;
            liveData.olb   = (ds >> 31) & 0x1;
            
            xSemaphoreGive(uartMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10Hz Poll
    }
}

void initTelemetry() {
    resetTelemetryBuffer();
    xTaskCreatePinnedToCore(TelemetryPollTask, "TelemPoll", 3072, NULL, 1, NULL, 0);
}

void resetTelemetryBuffer() {
    portENTER_CRITICAL(&motorMux);
    csvBuffer = "ts_ms,phase,val,pos_steps,spd_sps,real_rpm,pulse_cnt,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStartTime = millis();
    lastRecordMs = 0;
    portEXIT_CRITICAL(&motorMux);
}

void recordDataPoint(const char* phase, float val) {
    unsigned long now = millis();
    if (csvBuffer.length() > TELEM_MAX_BYTES || now - lastRecordMs < TELEM_INTERVAL_MS) return;
    lastRecordMs = now;
    if (!stepper) return;

    char line[180];
    snprintf(line, sizeof(line),
        "%lu,%s,%.0f,%ld,%d,%u,%lu,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStartTime, phase, val,
        stepper->getCurrentPosition(),
        (int)(stepper->getCurrentSpeedInMilliHz() / 1000),
        getTachoRpm(),
        getPulseCount(), // <--- Brückenschlag: Absolute Sensor-Umdrehungen
        liveData.sg, liveData.cs, liveData.cur_a, liveData.cur_b,
        liveData.stall, liveData.otpw, liveData.ot, liveData.ola, liveData.olb);
    
    portENTER_CRITICAL(&motorMux);
    csvBuffer += line;
    portEXIT_CRITICAL(&motorMux);
}

String getTelemetryCSV() {
    portENTER_CRITICAL(&motorMux);
    String copy = csvBuffer;
    portEXIT_CRITICAL(&motorMux);
    return copy;
}

uint16_t getLatestSgResult() { return liveData.sg; }
uint8_t  getLatestCsActual() { return liveData.cs; }
bool     isMotorStalled()    { return liveData.stall; }
