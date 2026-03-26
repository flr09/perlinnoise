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

// Tacho RPM tracking: period between consecutive LOW edges (one per revolution)
volatile unsigned long tachoPeriodMs = 0;
volatile unsigned long lastTachoLowMs = 0;

void IRAM_ATTR tachoISR() {
    // Direct hardware register read — no function call, no Flash access, truly ISR-safe.
    // pcnt_get_counter_value() is NOT IRAM_ATTR in ESP-IDF v4 → Flash fault when cache
    // is disabled (WiFi init, OTA). PCNT peripheral registers are always accessible.
    lastSensorRaw = (int16_t)(PCNT.cnt_unit[PCNT_UNIT_0].val & 0xFFFF);
    sensorHit = true;
    if (digitalRead(TACHO_PIN) == LOW) {
        pulseCount++;
        // millis() is ARDUINO_ISR_ATTR in ESP32 Arduino — safe to call from ISR
        unsigned long now = millis();
        if (lastTachoLowMs > 0) {
            unsigned long p = now - lastTachoLowMs;
            if (p >= 5) tachoPeriodMs = p;   // ignore bounces / noise < 5 ms apart
        }
        lastTachoLowMs = now;
    }
}

uint16_t getTachoRpm() {
    unsigned long period, lastT;
    portENTER_CRITICAL(&motorMux);
    period = tachoPeriodMs;
    lastT  = lastTachoLowMs;
    portEXIT_CRITICAL(&motorMux);
    if (period == 0 || millis() - lastT > 2000) return 0;
    return (uint16_t)min(9999UL, 60000UL / period);
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
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,real_rpm,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStart = millis(); lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES || now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;
    if (!stepper) return;
    char line[140];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val, stepper->getCurrentPosition(), (int)(stepper->getCurrentSpeedInMilliHz() / 1000),
        getTachoRpm(),
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
        // v3.6.12 coordinate change: triggerCenter is now always 0 (center = zero by definition).
        // Old firmware stored triggerCenter as absolute PCNT step position (~hundreds–thousands).
        // If triggerCenter != 0, this is old-format data → discard to prevent wrong moveTo(0).
        if (sys.cal[i].valid && sys.cal[i].triggerCenter != 0) {
            sys.cal[i] = CalibrationData(); // reset to default (valid=false)
        }
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

// --- CARDINAL ANGLES ---
// Moves to the nearest 0°/90°/180°/270° position (multiples of stepsPerRev/4).
// Call after setMicrosteps(64) so stepsPerRev=12800 and cardinals are consistent.
void gotoCardinal() {
    if (!stepper || !sys.cal[0].valid) return;
    long pos     = stepper->getCurrentPosition();
    long rev     = (long)stepsPerRev;
    long halfRev = rev / 2;
    long posInRev = pos % rev;
    if (posInRev < 0) posInRev += rev;
    long bestDelta = rev;
    int  bestQ     = 0;
    for (int q = 0; q < 4; q++) {
        long card  = (long)q * rev / 4;
        long delta = posInRev - card;
        if (delta >  halfRev) delta -= rev;
        if (delta < -halfRev) delta += rev;
        if (abs(delta) < abs(bestDelta)) { bestDelta = delta; bestQ = q; }
    }
    long target = pos - bestDelta;
    if (target == pos) return;
    addLog("→" + String(bestQ * 90) + "° (step " + String(target) + ")");
    stepper->setSpeedInHz((uint32_t)rpmToSps(300.0f));
    stepper->setAcceleration(8000);
    stepper->moveTo(target);
    while (stepper->isRunning()) { yield(); }
}

// --- HOMING ---
// Requires valid calibration. triggerStart stores the step offset from center where
// the sensor turns ON in CW direction (negative value). Center = 0°.
// v3.6.14: all stopMove() calls followed by while(isRunning()) — motor must be fully
// stopped before setCurrentPosition() or the next run command. PCNT ISR captures the
// exact sensor ON-edge so overshoot can be corrected in the position declaration.
void homeMotor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Homing...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    stepper->setAcceleration(2000);
    // Fast CW search for sensor
    stepper->setSpeedInHz(1200);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, (long)(stepsPerRev * 1.5f), 15000)) {
        addLog("Err: Sensor nicht gefunden");
        stepper->stopMove();
        while (stepper->isRunning()) { yield(); }
        return;
    }
    stepper->stopMove();
    while (stepper->isRunning()) { yield(); }  // H4: warten bis Motor vollständig steht
    // Back off CCW past sensor
    stepper->setSpeedInHz(400);
    stepper->runBackward();
    waitForSensorTimed(HIGH, (long)(stepsPerRev * 0.3f), 3000);
    stepper->stopMove();
    while (stepper->isRunning()) { yield(); }  // H4: warten bis Motor vollständig steht

    // PCNT sync vor Präzisions-Anfahrt — identisch mit characterizeSensor P2
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    pcntStepperBase = stepper->getCurrentPosition();
    sensorHit = false;

    // Slow CW precision approach — ISR captures exact ON-edge via PCNT register
    stepper->setSpeedInHz(200);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, (long)(stepsPerRev * 0.5f), 5000)) {
        addLog("Err: Sensor (langsam CW) nicht gefunden");
        stepper->stopMove();
        while (stepper->isRunning()) { yield(); }
        return;
    }
    long sCW_abs = (long)lastSensorRaw + pcntStepperBase;  // ISR-erfasste Einschaltposition
    stepper->stopMove();
    while (stepper->isRunning()) { yield(); }  // H4: Motor steht — setCurrentPosition erst jetzt sicher

    if (sys.cal[0].valid) {
        // Motor steht bei curPos; Sensor schaltete bei sCW_abs (ISR-präzise).
        // Überschuss nach dem Sensor = curPos - sCW_abs.
        // Position neu deklarieren: sCW_abs := triggerStart, curPos := triggerStart + Überschuss
        long curPos   = stepper->getCurrentPosition();
        long overshot = curPos - sCW_abs;
        stepper->setCurrentPosition(sys.cal[0].triggerStart + overshot);
        stepper->moveTo(0);  // → Sensormitte = 0°
        while (stepper->isRunning()) { yield(); }
        addLog("Home @0° (sCW=" + String(sCW_abs) + " over=" + String(overshot) + ")");
    } else {
        stepper->setCurrentPosition(0);
        addLog("Home @0° (keine Cal — Sensor-ON als 0)");
    }
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    // TPWMTHRS/TCOOLTHRS werden mit stepsPerRev=12800 (64MS) korrekt neu berechnet
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
}

// --- SENSOR CHARACTERIZATION ---
// Procedure:
//   P1: CCW 500sps until sensor (approaches from left — fast, just need to pass it)
//   P2: CW 200sps until sensor LOW  (consistent CW approach)
//   P3: Continue CW at 20sps through sensor → record ON (sCW) and OFF (eCW) steps
// Center = (sCW+eCW)/2 → declared as position 0 (0°)
// triggerStart = sCW - center  (negative offset, sensor turns on before center)
// triggerEnd   = eCW - center  (positive offset, sensor turns off after center)
void characterizeSensor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Sensor-Kalibrierung...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    stepper->setAcceleration(2000);

    // --- P1: CCW 500sps bis Sensor LOW — stellt sicher, wir sind links vom Sensor ---
    addLog("P1: CCW 500sps...");
    stepper->setSpeedInHz(500);
    stepper->runBackward();
    if (!waitForSensorTimed(LOW, (long)(stepsPerRev * 1.5f), 25000)) {
        addLog("Err: kein Sensor (CCW)"); stepper->stopMove(); return;
    }
    stepper->stopMove();
    while (stepper->isRunning()) { yield(); } // warten bis vollständig gestoppt
    // 0.5 rev CW Anlaufstrecke: sicher außerhalb Sensorzone für P2-Anfahrt
    stepper->setSpeedInHz(200);
    stepper->move((long)(stepsPerRev * 0.5f));
    while (stepper->isRunning()) { yield(); }

    // PCNT sync — Maximalweg ab hier: 0.7 rev CW + Sensorbreite (~50 steps) ≪ 32767
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    pcntStepperBase = stepper ? stepper->getCurrentPosition() : 0;

    // --- P2: CW 200sps bis Sensor LOW — ISR erfasst Einschaltposition ---
    addLog("P2: CW 200sps bis Sensor...");
    sensorHit = false;
    stepper->setSpeedInHz(200);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, stepsPerRev, 12000)) {
        addLog("Err: kein Sensor (CW)"); stepper->stopMove(); return;
    }
    long sCW = (long)lastSensorRaw + pcntStepperBase; // Einschaltpos (CW)

    // Ohne Stopp auf Kriechgang wechseln (FAS bremst sanft ab)
    stepper->setSpeedInHz(20);

    // --- P3: 20sps bis Sensor HIGH — ISR erfasst Ausschaltposition ---
    addLog("P3: 20sps durch Sensor...");
    sensorHit = false;
    if (!waitForSensorTimed(HIGH, (long)(stepsPerRev * 0.5f), 15000)) {
        addLog("Err: Sensor verlässt nicht"); stepper->stopMove(); return;
    }
    long eCW = (long)lastSensorRaw + pcntStepperBase; // Ausschaltpos (CW)
    stepper->stopMove();

    if (eCW <= sCW) { addLog("Err: eCW<=sCW — Sensor-Reihenfolge prüfen"); return; }
    long center = (sCW + eCW) / 2;
    float widthDeg = (float)(eCW - sCW) * 360.0f / (float)stepsPerRev;
    addLog("Sensor EIN=" + String(sCW) + " AUS=" + String(eCW)
           + " (" + String(eCW-sCW) + " steps, " + String(widthDeg,1) + "°)");
    addLog("Center=" + String(center) + " → wird 0°");

    // Offsets relativ zu Center speichern (triggerStart ist negativ)
    sys.cal[0].triggerStart  = sCW - center;   // z.B. -25 steps
    sys.cal[0].triggerCenter = 0;              // Center = 0° per Definition
    sys.cal[0].triggerEnd    = eCW - center;   // z.B. +25 steps
    sys.cal[0].valid = true;
    saveCalibration(0);

    // Zur Center-Position fahren und als 0 setzen
    stepper->moveTo(center);
    while (stepper->isRunning()) { yield(); }
    stepper->setCurrentPosition(0);

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    addLog("Kalibrierung OK — 0° gesetzt");
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
    // Fix 4: restore driver settings so values are active without re-power
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
    gotoCardinal();
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
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
    gotoCardinal();
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
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
    gotoCardinal();
}

void runKatapult(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    gotoCardinal(); // Start auf Kardinalpunkt
    addLog("=== KATAPULT ===");
    // clearTelemetry() is called by the caller (TaskCore1 standalone) or by the parcour sequence

    uint16_t runMA = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;

    // ===== PHASE A: Stall-Hunt per MS level =====
    addLog("--- A: Stall-Hunt ---");
    stepper->setAcceleration(30000);
    const uint16_t msLevels[6] = {32, 16, 8, 4, 2, 1};
    float msMaxRpm[6] = {0};
    uint16_t bestMs = 16;
    float bestAbsRpm = 0;

    for (int li = 0; li < 6; li++) {
        uint16_t ms = msLevels[li];
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
        setMicrosteps(ms);
        // Per-MS: recalculate TPWMTHRS/TCOOLTHRS for new stepsPerRev, then force SpreadCycle
        applyDriverSettings(runMA);
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
        }
        float testRpm = 600.0f;
        bool done = false;
        Serial.printf("[KAT-A] MS=%d (hunt from %.0f RPM)\n", ms, testRpm);
        while (testRpm <= 2500.0f && !done) {
            float tgtSps = rpmToSps(testRpm);
            // Reset tacho for fresh measurement — ISR: first LOW skips period, second sets it
            portENTER_CRITICAL(&motorMux);
            tachoPeriodMs = 0; lastTachoLowMs = 0;
            portEXIT_CRITICAL(&motorMux);
            stepper->setSpeedInHz((uint32_t)tgtSps);
            stepper->runForward();
            // Wait for motor to reach target speed (30k sps² accel, max 4 sec)
            unsigned long t0 = millis();
            while (millis() - t0 < 4000) {
                if (stepper->getCurrentSpeedInMilliHz() / 1000.0f >= tgtSps * 0.93f) break;
                yield();
            }
            // 400ms settle: allows tacho to measure 2+ revolutions even at 600 RPM (100ms/rev)
            delay(400);
            uint16_t realRpm   = getTachoRpm();
            uint16_t sg        = tCache.sg;
            float    actualRpm = spsToRpm(stepper->getCurrentSpeedInMilliHz() / 1000.0f);
            bool tachoStall = (realRpm == 0 || (float)realRpm < testRpm * 0.5f);
            bool sgStall    = (tCache.cs > 0 && sg < 20);
            Serial.printf("[KAT-A] MS=%d RPM=%.0f tach=%d SG=%d cs=%d tStall=%d\n",
                          ms, actualRpm, realRpm, sg, tCache.cs, tachoStall);
            recordTelemetry("HUNT", actualRpm);
            if (tachoStall || sgStall) {
                done = true;
                addLog("A: MS=" + String(ms) + " lim=" + String(actualRpm,0)
                       + " tach=" + String(realRpm) + " SG=" + String(sg));
            } else {
                msMaxRpm[li] = actualRpm;
                testRpm += 200.0f;
            }
        }
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
        if (!done) addLog("A: MS=" + String(ms) + " ok@2500");
        if (msMaxRpm[li] > bestAbsRpm) { bestAbsRpm = msMaxRpm[li]; bestMs = ms; }
        Serial.printf("[KAT-A] MS=%d max=%.0f RPM\n", ms, msMaxRpm[li]);
    }
    // Fix 2: use learned maxRpm as fallback, not hardcoded 2000
    if (bestAbsRpm < 200.0f) {
        bestMs = 16;
        bestAbsRpm = sys.cal[0].maxRpm > 600.0f ? sys.cal[0].maxRpm : 2000.0f;
        addLog("A: Fallback -> " + String(bestAbsRpm,0) + " RPM (aus Cal)");
    }
    addLog("A: bestMS=" + String(bestMs) + " @" + String(bestAbsRpm,0) + " RPM");

    // ===== PHASE B: 3×CW + 3×CCW full-load launch =====
    addLog("--- B: Launch (MS=" + String(bestMs) + ") ---");
    stepper->stopMove(); while (stepper->isRunning()) { yield(); }
    setMicrosteps(bestMs);
    float launchRpm = min(bestAbsRpm, 2500.0f);
    if (launchRpm < 500.0f) launchRpm = 2000.0f;
    stepper->setSpeedInHz((uint32_t)rpmToSps(launchRpm));
    stepper->setAcceleration(100000);
    for (int run = 0; run < 3; run++) {
        addLog("B: CW " + String(run+1) + "/3 @" + String(launchRpm,0) + " RPM");
        stepper->runForward();
        unsigned long t0 = millis();
        while (millis() - t0 < 2000) { recordTelemetry("LAUNCH_CW", launchRpm); yield(); }
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
        delay(150);
    }
    for (int run = 0; run < 3; run++) {
        addLog("B: CCW " + String(run+1) + "/3 @" + String(launchRpm,0) + " RPM");
        stepper->runBackward();
        unsigned long t0 = millis();
        while (millis() - t0 < 2000) { recordTelemetry("LAUNCH_CCW", launchRpm); yield(); }
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
        delay(150);
    }

    // ===== PHASE C: Coast + Ghost Mode =====
    addLog("--- C: Coast ---");
    stepper->setSpeedInHz((uint32_t)rpmToSps(launchRpm));
    stepper->setAcceleration(30000);
    stepper->runForward();
    delay(2000); // spin up to launch speed
    // Cut motor power — coasts freely under inertia
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.toff(0); xSemaphoreGive(uartMutex);
    }
    stepper->stopMove();
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    unsigned long coastStart = millis();
    uint32_t prevP = 0;
    while (millis() - coastStart < 3000) {
        delay(250);
        uint32_t p = getPulseCount();
        float rpmC = (float)(p - prevP) * (60000.0f / 250.0f);
        recordTelemetry("COAST", rpmC);
        Serial.printf("[KAT-C] t=%lums rpm~%.0f\n", millis()-coastStart, rpmC);
        prevP = p;
    }
    addLog("C: coast=" + String(getPulseCount()) + " rev/3s");

    // Ghost mode: StealthChop forced, find minimum working current
    addLog("--- C: Ghost (StealthChop) ---");
    stepper->stopMove(); while (stepper->isRunning()) { yield(); }
    setMicrosteps(16);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.toff(5);
        driverX.en_spreadCycle(false);  // StealthChop
        driverX.TPWMTHRS(0);           // TSTEP always > 0 → always StealthChop
        driverX.pwm_autoscale(true);
        xSemaphoreGive(uartMutex);
    }
    stepper->setSpeedInHz((uint32_t)rpmToSps(400.0f));
    stepper->setAcceleration(5000);
    uint16_t ghostCur = 400;
    uint16_t minGhostCur = 400;
    while (ghostCur >= 50) {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.rms_current(ghostCur); xSemaphoreGive(uartMutex);
        }
        portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
        stepper->runForward();
        delay(1500);
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
        uint32_t pulses = getPulseCount();
        float expected = 400.0f * 1.5f / 60.0f; // ~10 revs in 1.5s at 400 RPM
        bool ok = (float)pulses >= expected * 0.7f;
        Serial.printf("[Ghost] %dmA: %lu rev (need>=%.0f) %s\n", ghostCur, (unsigned long)pulses, expected*0.7f, ok?"OK":"FAIL");
        addLog("Ghost " + String(ghostCur) + "mA: " + String(pulses) + "r " + (ok?"OK":"FAIL"));
        recordTelemetry("GHOST", ghostCur);
        if (ok) { minGhostCur = ghostCur; ghostCur -= 50; }
        else break;
    }
    stepper->stopMove();
    addLog("Ghost min=" + String(minGhostCur) + "mA");

    // Restore normal settings
    setMicrosteps(64); // set MS first so applyDriverSettings gets correct stepsPerRev
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
    gotoCardinal(); // Ende auf Kardinalpunkt
    addLog("=== KATAPULT DONE ===");
}

void runPerformanceShow(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    gotoCardinal(); // Start auf Kardinalpunkt
    addLog("=== VORFÜHRUNG ===");

    // All vars declared before any potential goto
    uint16_t runMA    = sys.cal[0].learnedCurrentMA > 0  ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
    float    maxRpm   = sys.cal[0].maxRpm   > 600.0f     ? sys.cal[0].maxRpm            : 2000.0f;
    float    maxAcc   = sys.cal[0].maxAccel > 1000.0f    ? sys.cal[0].maxAccel           : 100000.0f;
    uint16_t silentMA = (uint16_t)(runMA / 2 > 150 ? runMA / 2 : 150);
    // Katapult-Finale vars (declared here to avoid jump-over-initialization with goto)
    unsigned long katT0        = 0;
    uint16_t      katCur       = 0;
    unsigned long katCoastStart= 0;
    uint32_t      katPrevP     = 0;

    // --- 1/4: SILENT — StealthChop, halber Strom, sehr langsam ---
    addLog("[1/4] Silent @150 RPM StealthChop " + String(silentMA) + "mA");
    setMicrosteps(16);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.toff(5); driverX.en_spreadCycle(false);
        driverX.TPWMTHRS(0);            // Force StealthChop at all speeds
        driverX.rms_current(silentMA);
        driverX.pwm_autoscale(true); xSemaphoreGive(uartMutex);
    }
    stepper->setSpeedInHz((uint32_t)rpmToSps(150.0f));
    stepper->setAcceleration(2000);
    stepper->move((long)(stepsPerRev * 3));
    while (stepper->isRunning()) { if (sys.pendingStop) goto cleanup; yield(); }
    delay(400);
    stepper->move(-(long)(stepsPerRev * 3));
    while (stepper->isRunning()) { if (sys.pendingStop) goto cleanup; yield(); }
    delay(600);

    // --- 2/4: AGILITY — auto StealthChop/SpreadCycle, gelernte Maximalwerte, schnelle Bursts ---
    // TPWMTHRS = maxRpm/2: below half-speed → StealthChop, above → SpreadCycle (TMC auto-switch)
    addLog("[2/4] Agility @" + String(maxRpm,0) + " RPM  a=" + String(maxAcc/1000.0f,0) + "k sps²");
    applyDriverSettings(runMA);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false);
        driverX.TPWMTHRS(rpmToTpwmthrs(maxRpm / 2.0f)); // auto-switch at half speed
        xSemaphoreGive(uartMutex);
    }
    stepper->setSpeedInHz((uint32_t)rpmToSps(maxRpm));
    stepper->setAcceleration((uint32_t)maxAcc);
    for (int run = 0; run < 3; run++) {
        addLog("  Burst " + String(run+1) + "/3  CW");
        stepper->move((long)(stepsPerRev * 4));
        while (stepper->isRunning()) { if (sys.pendingStop) goto cleanup; yield(); }
        delay(80);
        addLog("  Burst " + String(run+1) + "/3  CCW");
        stepper->move(-(long)(stepsPerRev * 4));
        while (stepper->isRunning()) { if (sys.pendingStop) goto cleanup; yield(); }
        delay(80);
    }
    delay(400);

    // --- 3/4: PRECISION — 4×90° Schritte, Winkelpunkte im UI leuchten auf ---
    addLog("[3/4] Precision: 4×90° (StealthChop)");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false);
        driverX.TPWMTHRS(rpmToTpwmthrs(100));
        driverX.rms_current(runMA); xSemaphoreGive(uartMutex);
    }
    stepper->setSpeedInHz((uint32_t)rpmToSps(300.0f));
    stepper->setAcceleration(8000);
    for (int step = 0; step < 4; step++) {
        stepper->move((long)(stepsPerRev / 4));   // genau +90°
        while (stepper->isRunning()) { if (sys.pendingStop) goto cleanup; yield(); }
        delay(900);                                // Pause: UI-Dot leuchtet auf
    }
    delay(300);

    // --- 4/4: KATAPULT FINALE — max Beschleunigung → Strom reduzieren → Coast ---
    addLog("[4/4] Katapult-Finale @" + String(maxRpm,0) + " RPM  " + String(runMA) + "mA→100mA→Coast");
    setMicrosteps(16);
    applyDriverSettings(runMA);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    stepper->setSpeedInHz((uint32_t)rpmToSps(maxRpm));
    stepper->setAcceleration((uint32_t)min(maxAcc, 100000.0f));
    stepper->runForward();
    // Warte bis Vollgas erreicht
    katT0 = millis();
    while (millis() - katT0 < 5000) {
        if (sys.pendingStop) goto cleanup;
        if (stepper->getCurrentSpeedInMilliHz() / 1000.0f >= rpmToSps(maxRpm) * 0.93f) break;
        yield();
    }
    // Stufenweise Stromreduktion während Vollbetrieb (runMA → ~70% → ~50% → ... → 100mA)
    katCur = runMA;
    while (katCur > 100) {
        if (sys.pendingStop) goto cleanup;
        katCur = (uint16_t)(katCur * 0.7f);
        if (katCur < 100) katCur = 100;
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.rms_current(katCur); xSemaphoreGive(uartMutex);
        }
        addLog("  cur=" + String(katCur) + "mA");
        delay(400);
    }
    // Coast: Strom abschneiden, Rotor läuft frei unter Trägheit aus
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.toff(0); xSemaphoreGive(uartMutex);
    }
    stepper->stopMove();
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    katCoastStart = millis(); katPrevP = 0;
    addLog("  Coast...");
    while (millis() - katCoastStart < 3000) {
        if (sys.pendingStop) goto cleanup;
        delay(250);
        { uint32_t p = getPulseCount();
          float rpmC = (float)(p - katPrevP) * (60000.0f / 250.0f);
          recordTelemetry("KAT_COAST", rpmC);
          katPrevP = p; }
    }
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.toff(5); xSemaphoreGive(uartMutex);
    }

cleanup:
    stepper->stopMove();
    sys.pendingStop = false;
    setMicrosteps(64);
    applyDriverSettings(runMA);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(false); xSemaphoreGive(uartMutex);
    }
    gotoCardinal(); // Ende auf Kardinalpunkt
    addLog("=== VORFÜHRUNG DONE ===");
}

void updateMotors() {
    if (sys.pendingStop && stepper) { stepper->stopMove(); sys.pendingStop = false; }
}
