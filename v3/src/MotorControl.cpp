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
uint32_t rpmToTpwmthrs(float rpm) {
    float usps = rpm * 3200.0f / 60.0f;
    return (usps < 1.0f) ? 0xFFFFF : (uint32_t)(12000000.0f / usps);
}

// --- INTERRUPT & TELEM CACHE ---
volatile long lastSensorPos = -1;
struct TelemCache { uint16_t sg=0; uint8_t cs=0; bool ola=false, olb=false, stall=false; } tCache;

void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) { lastSensorPos = steppers[0]->currentPosition(); }
}

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

void applyDriverSettings(uint16_t runMA) {
    float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
    driverX.rms_current(runMA, hF);
    driverX.iholddelay(10);
    driverX.SGTHRS(sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT);
    uint32_t tpwm = sys.cal[0].tpwmThrs > 0 ? sys.cal[0].tpwmThrs : rpmToTpwmthrs(200);
    driverX.TPWMTHRS(tpwm);
    driverX.TCOOLTHRS(rpmToTpwmthrs(50));
    driverX.pwm_autoscale(true);
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

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        driverX.begin(); driverX.toff(5); applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
        digitalWrite(ENABLE_PIN, LOW);
        addLog("M0 ON");
    } else {
        digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

void initMotors() {
    loadCalibration();
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW);
    driverX.begin(); driverX.toff(5); applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    pinMode(TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, CHANGE);
    steppers[0]->setMaxSpeed(4000); steppers[0]->setAcceleration(2000);
}

void homeMotor(int i) {
    if (i != 0) return;
    addLog("Homing M0...");
    driverX.en_spreadCycle(true);
    lastSensorPos = -1;
    steppers[0]->setSpeed(1200);
    while (lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    steppers[0]->setSpeed(-400);
    while (digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    steppers[0]->setCurrentPosition(0);
    steppers[0]->moveTo(sys.cal[0].triggerCenter);
    while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    driverX.en_spreadCycle(false);
    addLog("M0 Home.");
}

void characterizeSensor(int i) {
    if (i != 0) return;
    addLog("Mapping M0...");
    driverX.en_spreadCycle(true);
    lastSensorPos = -1; steppers[0]->setSpeed(150);
    while(lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    long sCW = lastSensorPos;
    while(digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    long eCW = steppers[0]->currentPosition();
    steppers[0]->move(3200); while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    lastSensorPos = -1; steppers[0]->setSpeed(-150);
    while(lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    long eCCW = lastSensorPos;
    while(digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    long sCCW = steppers[0]->currentPosition();
    sys.cal[0].triggerCenter = ((sCW+eCW)/2 + (sCCW+eCCW)/2) / 2;
    sys.cal[0].valid = true; saveCalibration(0);
    addLog("M0 Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

void learnSGProfile(int i) {
    if (i != 0) return;
    addLog("SG-Learn...");
    driverX.en_spreadCycle(true);
    uint32_t sps[] = {500, 1000, 2000, 4000};
    uint32_t sgSum = 0;
    for(int s=0; s<4; s++) {
        steppers[0]->setMaxSpeed(sps[s]); steppers[0]->move(3200);
        while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
        sgSum += driverX.SG_RESULT();
    }
    sys.cal[0].sgThrs = (sgSum / 4) * 0.6;
    saveCalibration(0);
    addLog("SGTHRS set to " + String(sys.cal[0].sgThrs));
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    float rpm = PARCOUR_RPM_START; bool failed = false;
    uint16_t cur = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
    addLog("Speed Parcour M0...");
    driverX.en_spreadCycle(true);
    while(rpm <= PARCOUR_RPM_MAX && !failed) {
        addLog("Try " + String(rpm,0) + " RPM");
        steppers[0]->setMaxSpeed(rpmToSps(rpm)); steppers[0]->setAcceleration(rpmToSps(rpm)*4);
        steppers[0]->move(6400);
        unsigned long lC = 0;
        while(steppers[0]->distanceToGo() != 0) { 
            steppers[0]->run(); recordTelemetry("SPEED", rpm);
            if (millis()-lC > 500) { updateTelemCache(); lC = millis(); }
            yield(); 
        }
        lastSensorPos = -1; steppers[0]->setSpeed(200);
        unsigned long sS = millis(); while(lastSensorPos == -1 && millis()-sS < 5000) { steppers[0]->runSpeed(); yield(); }
        long drift = (lastSensorPos != -1) ? abs(lastSensorPos % 3200) : 999;
        if (drift > 60) {
            if (cur + 100 <= MOTOR_CURRENT_MAX_MA) { cur += 100; driverX.rms_current(cur); addLog("Boost to " + String(cur) + "mA"); }
            else { failed = true; addLog("FAIL"); }
        } else { rpm += PARCOUR_RPM_STEP; }
    }
    sys.cal[0].maxRpm = rpm - PARCOUR_RPM_STEP; sys.cal[0].learnedCurrentMA = cur; saveCalibration(0);
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    addLog("Inertia M0...");
    float acc = 1000; bool failed = false;
    while(acc <= 40000 && !failed) {
        steppers[0]->setAcceleration(acc);
        steppers[0]->move(3200); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        steppers[0]->move(-3200); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
        if (abs(steppers[0]->currentPosition() % 3200) > 60) failed = true;
        else acc += 2000;
    }
    sys.cal[0].maxAccel = acc - 2000; saveCalibration(0);
}

void runCoastTest(int i) {
    addLog("Coast Test M0...");
    steppers[0]->setMaxSpeed(rpmToSps(400)); steppers[0]->setAcceleration(5000);
    steppers[0]->move(400); while(steppers[0]->distanceToGo()!=0) { steppers[0]->run(); yield(); }
    long pS = steppers[0]->currentPosition(); steppers[0]->setSpeed(rpmToSps(400));
    for(int s=0; s<800; s++) { steppers[0]->runSpeed(); yield(); }
    addLog("Coast drift: " + String(abs(steppers[0]->currentPosition() - pS - 800)));
}

void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) steppers[0]->run();
}
