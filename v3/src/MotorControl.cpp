#include "MotorControl.h"
#include <Preferences.h>
#include "driver/pcnt.h"   // ESP32 hardware pulse counter — ISR-safe position capture

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = NULL; // created in initMotors() — FreeRTOS heap not ready at global ctor time

TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);

FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

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

// PCNT-based edge capture: ISR stores the hardware counter value at the exact
// moment the sensor fires — no getCurrentPosition() call needed (not ISR-safe)
volatile int16_t lastSensorRaw = 0;   // raw PCNT value at last edge (16-bit signed)
volatile bool    sensorHit     = false;
long             pcntStepperBase = 0; // stepper position when PCNT was last cleared

void IRAM_ATTR tachoISR() {
    // Direct hardware register read — no function call, no Flash access, truly ISR-safe.
    // pcnt_get_counter_value() is NOT IRAM_ATTR in ESP-IDF v4 → Flash fault when cache
    // is disabled (WiFi init, OTA). PCNT peripheral registers are always accessible.
    lastSensorRaw = (int16_t)(PCNT.cnt_unit[PCNT_UNIT_0].val & 0xFFFF);
    sensorHit = true;
    if (digitalRead(TACHO_PIN) == LOW) { pulseCount++; }
}

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
    if (!stepper) return;
    char line[128];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val, stepper->getCurrentPosition(), (int)(stepper->getCurrentSpeedInMilliHz() / 1000),
        tCache.sg, tCache.cs, tCache.cur_a, tCache.cur_b,
        tCache.stall, tCache.otpw, tCache.ot, tCache.ola, tCache.olb);
    telemCSV += line;
}

void updateTelemCache() {
    // 100ms: applyDriverSettings writes ~8 UART registers, takes up to 16ms
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    if (ms == currentMicrosteps || (stepper && stepper->isRunning())) return;
    portENTER_CRITICAL(&motorMux);
    long oldPos = stepper ? stepper->getCurrentPosition() : 0;
    float factor = (float)ms / (float)currentMicrosteps;
    if (stepper) stepper->setCurrentPosition((long)((float)oldPos * factor));
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
    
    uartMutex = xSemaphoreCreateMutex(); // B1: must be created after FreeRTOS heap is ready
    // --- PCNT: configure BEFORE FastAccelStepper ---
    // The GPIO matrix has separate input and output paths.
    // PCNT uses the input path (GPIO → PCNT), RMT uses the output path (RMT → GPIO).
    // Order matters: pcnt_unit_config() calls gpio_output_disable() which calls
    // gpio_matrix_out(SIG_GPIO_OUT_IDX) — this would overwrite FAS's RMT routing if
    // called after. By configuring PCNT first, FAS's stepperConnectToPin() runs last
    // and sets up RMT routing correctly. PCNT input routing is untouched by FAS.
    pcnt_config_t pcnt_cfg = {};
    pcnt_cfg.pulse_gpio_num = X_STEP;
    pcnt_cfg.ctrl_gpio_num  = X_DIR;
    pcnt_cfg.pos_mode   = PCNT_COUNT_INC;    // rising edge of STEP → count
    pcnt_cfg.neg_mode   = PCNT_COUNT_DIS;    // falling edge → ignore
    pcnt_cfg.lctrl_mode = PCNT_MODE_REVERSE; // DIR LOW (backward) → count down
    pcnt_cfg.hctrl_mode = PCNT_MODE_KEEP;    // DIR HIGH (forward) → count up
    pcnt_cfg.counter_h_lim = 32767;
    pcnt_cfg.counter_l_lim = -32768;
    pcnt_cfg.unit    = PCNT_UNIT_0;
    pcnt_cfg.channel = PCNT_CHANNEL_0;
    pcnt_unit_config(&pcnt_cfg);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);

    // FAS runs after PCNT — stepperConnectToPin() routes RMT → X_STEP (output path)
    // and re-enables the output buffer. PCNT input routing for X_STEP is unaffected.
    engine.init();
    stepper = engine.stepperConnectToPin(X_STEP);
    if (stepper) {
        stepper->setDirectionPin(X_DIR);
        stepper->setEnablePin(ENABLE_PIN, true);
        stepper->setAutoEnable(false);
    }

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        driverX.begin(); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);

    pinMode(TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, CHANGE);
}

bool waitForSensorTimed(bool state, long maxSteps, unsigned long timeoutMs) {
    if (!stepper) return false; // B5: stepper may be NULL if engine init failed
    unsigned long start = millis();
    long startPos = stepper->getCurrentPosition();
    while (digitalRead(TACHO_PIN) != state) {
        if (millis() - start > timeoutMs || abs(stepper->getCurrentPosition() - startPos) > maxSteps) return false;
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
    stepper->setSpeedInHz(1200); stepper->setAcceleration(2000);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, 8000, 10000)) { addLog("Err: Not found"); stepper->stopMove(); return; }
    stepper->stopMove();
    stepper->setSpeedInHz(400); stepper->runBackward();
    waitForSensorTimed(HIGH, 1000, 2000); stepper->stopMove();
    stepper->setSpeedInHz(200); stepper->runForward();
    waitForSensorTimed(LOW, 500, 2000); stepper->stopMove();
    stepper->setCurrentPosition(0);
    stepper->moveTo(sys.cal[0].triggerCenter);
    while(stepper->isRunning()) { yield(); }
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
    stepper->setAcceleration(30000);

    // --- PHASE 1: 360° blind search ---
    // Scans one full revolution at 1200 sps. Guaranteed to find the sensor
    // regardless of start position. Eliminates the need to home before calibrating.
    addLog("360 scan...");
    stepper->setSpeedInHz(1200);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, (long)(stepsPerRev * 1.1f), 5000)) {
        addLog("Err: No sensor in 360"); stepper->stopMove(); return;
    }
    stepper->stopMove();
    // Back off 0.8 rev so the precise CW pass has a clean run-up from before the sensor.
    stepper->move(-(long)(stepsPerRev * 0.8f));
    while (stepper->isRunning()) { yield(); }

    // --- PCNT sync at this approach position ---
    // Max PCNT travel ahead: ~1 rev CW + 0.8 rev fwd + 1 rev CCW = ~2.8 rev = ~9000 steps
    // Well within ±32767 signed 16-bit range.
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    pcntStepperBase = stepper ? stepper->getCurrentPosition() : 0;

    // --- PHASE 2: CW precision pass ---
    sensorHit = false;
    stepper->setSpeedInHz(400); stepper->runForward();
    if (!waitForSensorTimed(LOW, stepsPerRev, 5000)) { addLog("Err: CW entry"); stepper->stopMove(); return; }
    long sCW = (long)lastSensorRaw + pcntStepperBase;

    sensorHit = false;
    stepper->setSpeedInHz(200); stepper->runForward(); waitForSensorTimed(HIGH, 1000, 3000);
    long eCW = (long)lastSensorRaw + pcntStepperBase;
    stepper->stopMove();
    addLog("CW: " + String(sCW) + "-" + String(eCW) + " (" + String(eCW-sCW) + " steps)");

    // Move 0.8 rev past the sensor before reversing
    stepper->move((long)(stepsPerRev * 0.8f)); while(stepper->isRunning()) { yield(); }

    // --- PHASE 3: CCW precision pass ---
    sensorHit = false;
    stepper->setSpeedInHz(400); stepper->runBackward();
    waitForSensorTimed(LOW, stepsPerRev, 5000);
    long eCCW = (long)lastSensorRaw + pcntStepperBase;

    sensorHit = false;
    stepper->setSpeedInHz(200); stepper->runBackward(); waitForSensorTimed(HIGH, 1000, 3000);
    long sCCW = (long)lastSensorRaw + pcntStepperBase;
    stepper->stopMove();
    addLog("CCW: " + String(sCCW) + "-" + String(eCCW) + " (" + String(eCCW-sCCW) + " steps)");

    // Center = average of CW midpoint and CCW midpoint (hysteresis-cancelled)
    sys.cal[0].triggerCenter = ((sCW+eCW)/2 + (sCCW+eCCW)/2) / 2;
    sys.cal[0].triggerStart  = sCW;
    sys.cal[0].triggerEnd    = eCW;
    sys.cal[0].valid = true; saveCalibration(0);
    addLog("Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
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
        stepper->setSpeedInHz((uint32_t)sps);
        stepper->runForward(); // C2: call once after speed change, not inside the loop
        unsigned long start = millis();
        unsigned long lastSG = 0;
        while(millis() - start < 2000) {
            if (millis() - lastSG >= 50) {
                // Use cache — no direct UART read from Core 1
                sgSum += (float)tCache.sg; samples++;
                lastSG = millis();
            }
            yield();
        }
    }
    stepper->stopMove(); // B2: stop before setMicrosteps — isRunning() guard would skip it otherwise
    if(samples > 0) sys.cal[0].sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
    saveCalibration(0);
    // L1: Set 64 MS first (stepsPerRev=12800), THEN apply settings so TPWMTHRS is correct
    setMicrosteps(64);
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    // E1: Set high acceleration for parcour ramps (homeMotor uses 2000 sps² — far too slow)
    stepper->setAcceleration(30000);
    float rpm = (sys.cal[0].maxRpm > 250.0f) ? sys.cal[0].maxRpm * 0.8f : 200.0f;
    bool failed = false;
    uint16_t cur = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
    addLog("Speed Parcour...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    while(rpm <= PARCOUR_RPM_MAX && !failed) {
        addLog("Try " + String(rpm,0) + " RPM");
        float targetSps = rpmToSps(rpm);
        stepper->setSpeedInHz((uint32_t)targetSps);
        stepper->runForward();
        // E2: Wait until motor actually reaches target speed (not just a fixed 500ms)
        // Timeout = time to ramp + 500ms buffer (at 30000 sps²)
        unsigned long settleStart = millis();
        uint32_t rampTimeMs = (uint32_t)(targetSps / 30000.0f * 1000.0f) + 500;
        while (millis() - settleStart < rampTimeMs) {
            if (stepper->getCurrentSpeedInMilliHz() / 1000.0f >= targetSps * 0.95f) break;
            yield();
        }
        float reachedSps = stepper->getCurrentSpeedInMilliHz() / 1000.0f;
        float reachedRpm = spsToRpm(reachedSps);
        addLog("Soll=" + String(rpm,0) + " Ist=" + String(reachedRpm,0) + " RPM");
        portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
        // Adaptive window: 3 full revolutions at target RPM, min 500ms
        uint32_t win = max(500u, (uint32_t)(3.0f * 60000.0f / rpm));
        unsigned long start = millis();
        while(millis() - start < win) {
            recordTelemetry("SPEED", rpm);
            yield();
        }
        stepper->stopMove();
        float actualRpm = (float)getPulseCount() * 60000.0f / (float)win;
        // E3: Only boost if motor physically can't reach speed (not if ramp was too slow)
        if (reachedRpm < rpm * 0.85f) {
            if (cur + 100 <= MOTOR_CURRENT_MAX_MA) {
                cur += 100;
                if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    driverX.rms_current(cur); xSemaphoreGive(uartMutex);
                }
                addLog("Boost " + String(cur) + "mA");
            } else { failed = true; addLog("FAIL at " + String(rpm,0) + " RPM"); }
        } else { sys.cal[0].maxRpm = rpm; rpm += PARCOUR_RPM_STEP; }
    }
    saveCalibration(0);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    stepper->setAcceleration(2000); // restore homeMotor default
    setMicrosteps(64);
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Inertia...");
    float acc = 1000; bool failed = false;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 400.0f;
    stepper->setSpeedInHz((uint32_t)rpmToSps(testSpd));
    while(acc <= 40000 && !failed) {
        stepper->setAcceleration(acc);
        portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
        stepper->move(stepsPerRev * 2); while(stepper->isRunning()) { yield(); }
        stepper->move(-stepsPerRev * 2); while(stepper->isRunning()) { yield(); }
        if (getPulseCount() < 4) failed = true;
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
    stepper->setSpeedInHz((uint32_t)rpmToSps(400));
    unsigned long start = millis();
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    stepper->runForward();
    while(millis() - start < 1000) { yield(); }
    stepper->stopMove(); // B3: must stop or subsequent setMicrosteps is blocked
    addLog("Pulses: " + String(getPulseCount()));
    setMicrosteps(64);
}

void updateMotors() {
    if (sys.pendingStop && stepper) { stepper->stopMove(); sys.pendingStop = false; }
}
