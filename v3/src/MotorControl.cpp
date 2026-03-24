#include "MotorControl.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;

TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

float rpmToSps(float rpm)  { return (rpm * 3200.0f) / 60.0f; }
float spsToRpm(float sps)  { return (sps * 60.0f) / 3200.0f; }

uint32_t rpmToTpwmthrs(float rpm) {
    float usps = rpm * 3200.0f / 60.0f;
    return (usps < 1.0f) ? 0xFFFFF : (uint32_t)(12000000.0f / usps);
}

// --- ATOMIC ISR CAPTURE (Fix C4, m2) ---
volatile int32_t isrPos = -1;
volatile uint32_t pulseCount = 0;

void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) {
        // We cannot call AccelStepper methods here, so we'd need a direct pointer 
        // to the position variable if we wanted perfect precision. 
        // For now, we use a flag and capture in the loop or use a simpler approach.
        pulseCount++;
    }
}

// Helper to get pulse count atomically
uint32_t getPulseCount() {
    uint32_t c;
    portENTER_CRITICAL(&motorMux);
    c = pulseCount;
    portEXIT_CRITICAL(&motorMux);
    return c;
}

// --- TELEMETRY CACHE ---
struct TelemCache { uint16_t sg=0; uint8_t cs=0; bool ola=false, olb=false, stall=false; } tCache;
String telemCSV = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,stall,ola,olb\n";
    telemStart = millis(); lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES || now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;
    char line[100];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d\n",
        now - telemStart, phase, val, steppers[0]->currentPosition(), (int)steppers[0]->speed(),
        tCache.sg, tCache.cs, tCache.stall, tCache.ola, tCache.olb);
    telemCSV += line;
}

void updateTelemCache() {
    tCache.sg = driverX.SG_RESULT();
    tCache.cs = driverX.cs_actual();
    uint32_t ds = driverX.DRV_STATUS();
    tCache.stall = (ds & 0x1); tCache.ola = (ds >> 30) & 0x1; tCache.olb = (ds >> 31) & 0x1;
}

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

// --- DRIVER MGMT ---
void applyDriverSettings(uint16_t runMA) {
    float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
    driverX.rms_current(runMA, hF);
    driverX.iholddelay(10);
    driverX.SGTHRS(sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT);
    
    // Fix M1: StealthChop only at very low speeds/standstill
    uint32_t tpwm = rpmToTpwmthrs(100); 
    driverX.TPWMTHRS(tpwm);
    driverX.en_spreadCycle(false); // Enable automatic switching
    
    driverX.TCOOLTHRS(rpmToTpwmthrs(50));
    driverX.pwm_autoscale(true);
}

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        digitalWrite(ENABLE_PIN, LOW); 
        driverX.begin(); driverX.toff(5);
        applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
        addLog("M0 ON");
    } else {
        driverX.toff(0); digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

// --- CALIBRATION ---
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
    pinMode(ENABLE_PIN, OUTPUT);
    setMotorPower(0, true);
    pinMode(TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, FALLING);
    steppers[0]->setMaxSpeed(4000); steppers[0]->setAcceleration(2000);
}

bool waitForSensorRobust(bool state, unsigned long timeoutMs) {
    unsigned long start = millis();
    while (digitalRead(TACHO_PIN) != state) {
        if (millis() - start > timeoutMs) return false;
        steppers[0]->runSpeed();
        yield();
    }
    return true;
}

void homeMotor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    addLog("Homing M0...");
    driverX.en_spreadCycle(true);
    steppers[0]->setSpeed(1200);
    if (!waitForSensorRobust(LOW, 15000)) { addLog("Err: Home Timeout"); return; }
    steppers[0]->setSpeed(-400); waitForSensorRobust(HIGH, 3000);
    steppers[0]->setSpeed(100);  waitForSensorRobust(LOW, 3000);
    steppers[0]->setCurrentPosition(0);
    steppers[0]->moveTo(sys.cal[0].triggerCenter);
    while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    driverX.en_spreadCycle(false);
    addLog("M0 Home.");
}

void characterizeSensor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    addLog("Mapping M0...");
    driverX.en_spreadCycle(true);
    steppers[0]->setSpeed(150);
    if (!waitForSensorRobust(LOW, 15000)) { addLog("Err: Mapping fail"); return; }
    long sCW = steppers[0]->currentPosition();
    waitForSensorRobust(HIGH, 5000);
    long eCW = steppers[0]->currentPosition();
    steppers[0]->move(3200); while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    steppers[0]->setSpeed(-150);
    waitForSensorRobust(LOW, 15000);
    long eCCW = steppers[0]->currentPosition();
    waitForSensorRobust(HIGH, 5000);
    long sCCW = steppers[0]->currentPosition();
    sys.cal[0].triggerCenter = ((sCW+eCW)/2 + (sCCW+eCCW)/2) / 2;
    sys.cal[0].valid = true; saveCalibration(0);
    addLog("M0 Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

// --- ACTUAL RPM MEASUREMENT (The Gamechanger) ---
float measureActualRpm(float cmdSps, uint32_t windowMs) {
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    steppers[0]->setSpeed(cmdSps);
    unsigned long start = millis();
    while(millis() - start < windowMs) {
        steppers[0]->runSpeed();
        yield();
    }
    uint32_t pulses = getPulseCount();
    return (float)pulses * 60000.0f / (float)windowMs;
}

void learnSGProfile(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    addLog("SG-Learn (SpreadCycle)...");
    driverX.en_spreadCycle(true); // Fix 2: Force SpreadCycle for measurement
    
    // Fix C1: Measure at 100-400 RPM (SpreadCycle range)
    float testRpms[] = {100, 200, 300, 400};
    float sgSum = 0; int samples = 0;
    
    for(int s=0; s<4; s++) {
        float sps = rpmToSps(testRpms[s]);
        steppers[0]->setMaxSpeed(sps);
        steppers[0]->setAcceleration(sps*4);
        steppers[0]->move(6400); // 2 turns
        while(steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            // Fix C1: Sample DURING move
            if(abs(steppers[0]->speed()) > sps*0.8f) {
                sgSum += (float)driverX.SG_RESULT();
                samples++;
            }
            yield();
        }
        addLog("SG @" + String(testRpms[s],0) + "RPM: " + String(driverX.SG_RESULT()));
    }
    // Fix C2: Float precision calculation
    if(samples > 0) sys.cal[0].sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
    else sys.cal[0].sgThrs = MOTOR_SGTHRS_DEFAULT;
    
    saveCalibration(0);
    driverX.en_spreadCycle(false);
    addLog("SGTHRS set to " + String(sys.cal[0].sgThrs));
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    // Start at 200 RPM or 80% of last max
    float rpm = (sys.cal[0].maxRpm > 400) ? sys.cal[0].maxRpm * 0.8f : 200.0f;
    bool failed = false;
    uint16_t cur = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
    
    addLog("Speed Parcour M0...");
    driverX.en_spreadCycle(true);
    
    while(rpm <= PARCOUR_RPM_MAX && !failed) {
        float cmdSps = rpmToSps(rpm);
        addLog("Try " + String(rpm,0) + " RPM");
        
        float actualRpm = measureActualRpm(cmdSps, 2000); // 2 second window
        addLog("Ist=" + String(actualRpm,0) + " Soll=" + String(rpm,0));
        
        // Fix Bug 1 from Laufanalyse: Real RPM validation
        if (actualRpm < rpm * 0.7f) { // Allow 30% slip before failing
            if (cur + 100 <= MOTOR_CURRENT_MAX_MA) {
                cur += 100; driverX.rms_current(cur);
                addLog("Boost to " + String(cur) + "mA, retry...");
            } else {
                failed = true;
                addLog("FAIL at " + String(rpm,0));
            }
        } else {
            sys.cal[0].maxRpm = rpm; // Fix M2: Store CURRENT successful RPM
            rpm += PARCOUR_RPM_STEP;
        }
    }
    saveCalibration(0);
    driverX.en_spreadCycle(false);
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    addLog("Inertia M0...");
    float acc = 1000; bool failed = false;
    // Fix M3: Set Speed for Inertia test
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 400.0f;
    steppers[0]->setMaxSpeed(rpmToSps(testSpd));

    while(acc <= 40000 && !failed) {
        steppers[0]->setAcceleration(acc);
        steppers[0]->move(3200); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        steppers[0]->move(-3200); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        
        // Use measureActualRpm logic for inertia check
        float checkRpm = measureActualRpm(rpmToSps(testSpd), 1000);
        if (checkRpm < testSpd * 0.8f) failed = true;
        else acc += 2000;
    }
    sys.cal[0].maxAccel = acc - 2000; saveCalibration(0);
}

void runCoastTest(int i) {
    // Fix M4: Guard checks
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    addLog("Coast Test M0...");
    steppers[0]->setMaxSpeed(rpmToSps(400)); steppers[0]->setAcceleration(5000);
    steppers[0]->move(400); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
    long pS = steppers[0]->currentPosition(); steppers[0]->setSpeed(rpmToSps(400));
    for(int s=0; s<800; s++) { steppers[0]->runSpeed(); yield(); }
    addLog("Coast drift: " + String(abs(steppers[0]->currentPosition() - pS - 800)));
}

void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) {
        // Fix m1: Removed redundant digitalWrite
        steppers[0]->run();
    }
}
