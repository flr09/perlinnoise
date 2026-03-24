#include "MotorControl.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = xSemaphoreCreateMutex();

TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

uint16_t currentMicrosteps = 64;
uint16_t stepsPerRev = 12800;

float rpmToSps(float rpm)  { return (rpm * (float)stepsPerRev) / 60.0f; }
float spsToRpm(float sps)  { return (sps * 60.0f) / (float)stepsPerRev; }

uint32_t rpmToTpwmthrs(float rpm) {
    float usps = rpm * (float)stepsPerRev / 60.0f;
    return (usps < 1.0f) ? 0xFFFFF : (uint32_t)(12000000.0f / usps);
}

// --- ATOMIC ISR ---
volatile uint32_t pulseCount = 0;
void IRAM_ATTR tachoISR() { if (digitalRead(TACHO_PIN) == LOW) { pulseCount++; } }

uint32_t getPulseCount() {
    uint32_t c; portENTER_CRITICAL(&motorMux); c = pulseCount; portEXIT_CRITICAL(&motorMux);
    return c;
}

// --- TELEMETRY CACHE ---
struct TelemCache { 
    uint16_t sg=0; uint8_t cs=0; int16_t cur_a=0, cur_b=0;
    bool stall=false, otpw=false, ot=false, ola=false, olb=false; 
} tCache;

String telemCSV = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStart = millis(); lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES || now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;
    char line[128];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val, steppers[0]->currentPosition(), (int)steppers[0]->speed(),
        tCache.sg, tCache.cs, tCache.cur_a, tCache.cur_b,
        tCache.stall, tCache.otpw, tCache.ot, tCache.ola, tCache.olb);
    telemCSV += line;
}

void updateTelemCache() {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        tCache.sg = driverX.SG_RESULT();
        tCache.cs = driverX.cs_actual();
        uint32_t msc = driverX.MSCURACT();
        tCache.cur_a = (int16_t)(msc & 0x1FF); if (tCache.cur_a > 255) tCache.cur_a -= 512;
        tCache.cur_b = (int16_t)((msc >> 16) & 0x1FF); if (tCache.cur_b > 255) tCache.cur_b -= 512;
        uint32_t ds = driverX.DRV_STATUS();
        tCache.stall = (ds & 0x1); tCache.otpw = driverX.otpw(); tCache.ot = driverX.ot();
        tCache.ola = (ds >> 30) & 0x1; tCache.olb = (ds >> 31) & 0x1;
        xSemaphoreGive(uartMutex);
    }
}

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

void applyDriverSettings(uint16_t runMA) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
        driverX.rms_current(runMA, hF);
        driverX.microsteps(currentMicrosteps);
        driverX.iholddelay(10);
        driverX.SGTHRS(sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT);
        driverX.TPWMTHRS(rpmToTpwmthrs(100));
        driverX.en_spreadCycle(false);
        driverX.TCOOLTHRS(rpmToTpwmthrs(50));
        driverX.pwm_autoscale(true);
        xSemaphoreGive(uartMutex);
    }
}

void setMicrosteps(uint16_t ms) {
    if (ms == currentMicrosteps) return;
    portENTER_CRITICAL(&motorMux);
    float factor = (float)ms / (float)currentMicrosteps;
    steppers[0]->setCurrentPosition((long)((float)steppers[0]->currentPosition() * factor));
    steppers[0]->setSpeed(steppers[0]->speed() * factor);
    currentMicrosteps = ms;
    stepsPerRev = 200 * ms;
    portEXIT_CRITICAL(&motorMux);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.microsteps(ms);
        xSemaphoreGive(uartMutex);
    }
}

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        digitalWrite(ENABLE_PIN, LOW); 
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.toff(5); xSemaphoreGive(uartMutex);
        }
        applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
        addLog("M0 ON");
    } else {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.toff(0); xSemaphoreGive(uartMutex);
        }
        digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

void saveCalibration(int i) {
    prefs.begin("cal", false);
    String key = "m" + String(i) + "_" + String(sizeof(CalibrationData));
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("cal", true);
    for (int i = 0; i < 4; i++) {
        String key = "m" + String(i) + "_" + String(sizeof(CalibrationData));
        if (prefs.getBytesLength(key.c_str()) == sizeof(CalibrationData))
            prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    }
    prefs.end();
}

void initMotors() {
    loadCalibration();
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        driverX.begin(); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    pinMode(TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, FALLING);
    steppers[0]->setMaxSpeed(4000); steppers[0]->setAcceleration(2000);
}

// Helper for timed search with step limit
bool waitForSensorTimed(bool state, long maxSteps, unsigned long timeoutMs) {
    unsigned long start = millis();
    long startPos = steppers[0]->currentPosition();
    while (digitalRead(TACHO_PIN) != state) {
        if (millis() - start > timeoutMs || abs(steppers[0]->currentPosition() - startPos) > maxSteps) return false;
        steppers[0]->runSpeed();
        yield();
    }
    return true;
}

void homeMotor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Homing...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    steppers[0]->setSpeed(1200);
    // Limit to 2.5 turns (8000 steps)
    if (!waitForSensorTimed(LOW, 8000, 10000)) { addLog("Err: Not found"); return; }
    steppers[0]->setSpeed(-400); waitForSensorTimed(HIGH, 1000, 2000);
    steppers[0]->setSpeed(200);  waitForSensorTimed(LOW, 500, 2000);
    steppers[0]->setCurrentPosition(0);
    steppers[0]->moveTo(sys.cal[0].triggerCenter);
    while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    addLog("Home.");
}

void characterizeSensor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Mapping...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    steppers[0]->setSpeed(400);
    if (!waitForSensorTimed(LOW, 8000, 10000)) { addLog("Err: Not found"); return; }
    long sCW = steppers[0]->currentPosition();
    steppers[0]->setSpeed(200); waitForSensorTimed(HIGH, 1000, 3000);
    long eCW = steppers[0]->currentPosition();
    // Quick move to other side
    steppers[0]->move(stepsPerRev * 0.8f); while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    steppers[0]->setSpeed(-400); waitForSensorTimed(LOW, 4000, 5000);
    long eCCW = steppers[0]->currentPosition();
    steppers[0]->setSpeed(200); waitForSensorTimed(HIGH, 1000, 3000);
    long sCCW = steppers[0]->currentPosition();
    sys.cal[0].triggerCenter = ((sCW+eCW)/2 + (sCCW+eCCW)/2) / 2;
    sys.cal[0].valid = true; saveCalibration(0);
    addLog("Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

float measureActualRpm(float cmdSps, uint32_t windowMs) {
    steppers[0]->setSpeed(cmdSps);
    unsigned long settle = millis();
    while(millis() - settle < 500) { steppers[0]->runSpeed(); yield(); }
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    unsigned long start = millis();
    while(millis() - start < windowMs) { steppers[0]->runSpeed(); yield(); }
    return (float)getPulseCount() * 60000.0f / (float)windowMs;
}

void learnSGProfile(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("SG-Learn...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    float testRpms[] = {150, 250, 350, 450};
    float sgSum = 0; int samples = 0;
    for(int s=0; s<4; s++) {
        float sps = rpmToSps(testRpms[s]);
        steppers[0]->setSpeed(sps);
        unsigned long settle = millis();
        while(millis() - settle < 800) { steppers[0]->runSpeed(); yield(); }
        unsigned long start = millis();
        while(millis() - start < 1500) {
            steppers[0]->runSpeed();
            if (xSemaphoreTake(uartMutex, 0) == pdTRUE) {
                sgSum += (float)driverX.SG_RESULT(); samples++;
                xSemaphoreGive(uartMutex);
            }
            yield();
        }
    }
    if(samples > 0) sys.cal[0].sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
    saveCalibration(0);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    addLog("SGTHRS: " + String(sys.cal[0].sgThrs));
}

// --- PARCOUR RPM BEREICH ---
#ifndef PARCOUR_RPM_MAX
#define PARCOUR_RPM_START   300.0f
#define PARCOUR_RPM_MAX    2500.0f
#define PARCOUR_RPM_STEP     100.0f
#define PARCOUR_RPM_FINE      10.0f
#endif

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    float rpm = 200.0f; bool failed = false;
    uint16_t cur = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
    addLog("Speed Parcour...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    while(rpm <= PARCOUR_RPM_MAX && !failed) {
        float cmdSps = rpmToSps(rpm);
        addLog("Try " + String(rpm,0) + " RPM");
        float actualRpm = measureActualRpm(cmdSps, 2000);
        addLog("Ist=" + String(actualRpm,0) + " Soll=" + String(rpm,0));
        if (actualRpm < rpm * 0.7f) {
            if (cur + 100 <= MOTOR_CURRENT_MAX_MA) {
                cur += 100;
                if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    driverX.rms_current(cur); xSemaphoreGive(uartMutex);
                }
                addLog("Boost " + String(cur) + "mA");
            } else { failed = true; addLog("FAIL"); }
        } else { sys.cal[0].maxRpm = rpm; rpm += PARCOUR_RPM_STEP; }
    }
    saveCalibration(0);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Inertia...");
    float acc = 1000; bool failed = false;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 400.0f;
    steppers[0]->setMaxSpeed(rpmToSps(testSpd));
    while(acc <= 40000 && !failed) {
        steppers[0]->setAcceleration(acc);
        steppers[0]->move(stepsPerRev); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        steppers[0]->move(-stepsPerRev); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        if (measureActualRpm(rpmToSps(testSpd), 1000) < testSpd * 0.8f) failed = true;
        else acc += 2000;
    }
    sys.cal[0].maxAccel = acc - 2000; saveCalibration(0);
    setMicrosteps(64);
}

void runCoastTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Coast...");
    steppers[0]->setMaxSpeed(rpmToSps(400)); steppers[0]->setAcceleration(5000);
    steppers[0]->move(400); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
    long pS = steppers[0]->currentPosition(); steppers[0]->setSpeed(rpmToSps(400));
    for(int s=0; s<800; s++) { steppers[0]->runSpeed(); yield(); }
    addLog("Drift: " + String(abs(steppers[0]->currentPosition() - pS - 800)));
    setMicrosteps(64);
}

void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) { steppers[0]->run(); }
}
