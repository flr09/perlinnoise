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

float rpmToSps(float rpm) { return (rpm * 3200.0f) / 60.0f; }

// --- TELEMETRY ---
String telemCSV = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStart = millis();
    lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES) return;
    if (now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;

    long pos = steppers[0]->currentPosition();
    int  spd = (int)steppers[0]->speed();
    uint16_t sg = driverX.SG_RESULT();
    uint8_t  cs = driverX.cs_actual();
    uint32_t msc = driverX.MSCURACT();
    int16_t cur_a = (int16_t)(msc & 0x1FF);        if (cur_a > 255) cur_a -= 512;
    int16_t cur_b = (int16_t)((msc >> 16) & 0x1FF); if (cur_b > 255) cur_b -= 512;
    uint8_t flags = (sg==0?1:0) | (driverX.otpw()?2:0)
                  | (driverX.ot()?4:0) | (driverX.ola()?8:0) | (driverX.olb()?16:0);

    char line[128];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val, pos, spd, sg, cs, cur_a, cur_b,
        (flags>>0)&1, (flags>>1)&1, (flags>>2)&1, (flags>>3)&1, (flags>>4)&1);
    telemCSV += line;
}

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

void saveCalibration(int i) {
    prefs.begin("calib", false);
    String key = "m" + String(i);
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("calib", true);
    for(int i=0; i<4; i++) {
        String key = "m" + String(i);
        if (prefs.isKey(key.c_str())) {
            prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
        }
    }
    prefs.end();
}

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        // Full re-init: toff(0) may have left driver in undefined state
        driverX.begin();
        driverX.toff(5);
        driverX.rms_current(600);
        driverX.microsteps(16);
        driverX.pwm_autoscale(true);
        digitalWrite(ENABLE_PIN, LOW);
        addLog("M0 Power ON");
    } else {
        digitalWrite(ENABLE_PIN, HIGH); // Hardware disable, coils de-energized
        addLog("M0 Power OFF");
    }
}

void setSpreadCycle(int i, bool enable) {
    if (i == 0) driverX.en_spreadCycle(enable);
}

void initMotors() {
    loadCalibration();
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    
    pinMode(ENABLE_PIN, OUTPUT); 
    digitalWrite(ENABLE_PIN, LOW); // LOW = Enabled on E4

    driverX.begin(); driverX.toff(5); driverX.rms_current(600); driverX.microsteps(16); driverX.pwm_autoscale(true);
    
    pinMode(TACHO_PIN, INPUT_PULLUP);
    
    steppers[0]->setMaxSpeed(4000);
    steppers[0]->setAcceleration(2000);
}

bool waitForSensor(int i, bool state, unsigned long timeoutMs) {
    unsigned long start = millis();
    while (digitalRead(TACHO_PIN) != state) {
        if (millis() - start > timeoutMs) return false;
        steppers[i]->runSpeed();
        yield();
    }
    return true;
}

void homeMotor(int i) {
    if (i != 0) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    addLog("Homing M0...");
    setSpreadCycle(0, true);
    steppers[0]->setSpeed(1200);
    if (!waitForSensor(0, LOW, 15000)) { addLog("Err: M0 Timeout!"); return; }
    
    steppers[0]->setSpeed(-400);
    waitForSensor(0, HIGH, 3000);
    steppers[0]->setSpeed(100);
    waitForSensor(0, LOW, 3000);

    steppers[0]->setCurrentPosition(0);
    long target = sys.cal[0].valid ? sys.cal[0].triggerCenter : 0;
    steppers[0]->moveTo(target);
    unsigned long mS = millis();
    while(steppers[0]->distanceToGo() != 0 && millis()-mS < 5000) { steppers[0]->run(); yield(); }
    setSpreadCycle(0, false);
    addLog("M0 Home.");
}

void characterizeSensor(int i) {
    if (i != 0) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    addLog("Mapping M0...");
    setSpreadCycle(0, true);
    
    steppers[0]->setSpeed(150);
    if(!waitForSensor(0, LOW, 15000)) { addLog("Err: CW fail"); return; }
    long sCW = steppers[0]->currentPosition();
    if(!waitForSensor(0, HIGH, 5000)) { addLog("Err: CW End fail"); return; }
    long eCW = steppers[0]->currentPosition();
    
    // Full revolution forward to approach sensor from the other side (CCW)
    steppers[0]->move(3200); while(steppers[0]->run()) { yield(); }
    steppers[0]->setSpeed(-150);
    if(!waitForSensor(0, LOW, 25000)) { addLog("Err: CCW fail"); return; }
    long eCCW = steppers[0]->currentPosition();
    if(!waitForSensor(0, HIGH, 5000)) { addLog("Err: CCW End fail"); return; }
    long sCCW = steppers[0]->currentPosition();

    sys.cal[0].triggerCenter = ((sCW+eCW)/2 + (sCCW+eCCW)/2) / 2;
    sys.cal[0].valid = true;
    saveCalibration(0);
    addLog("M0 Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) { addLog("Error: Calib M0 first!"); return; }
    if (!sys.m[0].enabled) setMotorPower(0, true);
    float rpm = 300.0f; float lastGood = rpm; bool failed = false;
    clearTelemetry();
    addLog("Speed Parcours M0...");

    while(rpm <= 2500.0f && !failed) {
        addLog("Try " + String(rpm,0) + " RPM");
        steppers[0]->setMaxSpeed(rpmToSps(rpm));
        steppers[0]->setAcceleration(rpmToSps(rpm)*4);
        steppers[0]->move(6400);
        while(steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            recordTelemetry("SPEED", rpm);
            yield();
        }

        setSpreadCycle(0, true);
        steppers[0]->setSpeed(200);
        unsigned long sS = millis(); bool found = false;
        while(millis() - sS < 5000) {
            if(digitalRead(TACHO_PIN) == LOW) { found = true; break; }
            steppers[0]->runSpeed();
            recordTelemetry("DRIFT", rpm);
            yield();
        }
        long drift = abs(steppers[0]->currentPosition() % 3200);
        if (!found || drift > 60) { failed = true; addLog("FAIL at " + String(rpm,0)); }
        else { lastGood = rpm; rpm += 100.0f; }
        setSpreadCycle(0, false);
    }

    if (failed) {
        rpm = lastGood + 10.0f; failed = false;
        addLog("Fine-tuning...");
        while(rpm < (lastGood + 100.0f) && !failed) {
            steppers[0]->setMaxSpeed(rpmToSps(rpm));
            steppers[0]->move(3200);
            while(steppers[0]->distanceToGo() != 0) {
                steppers[0]->run();
                recordTelemetry("FINE", rpm);
                yield();
            }
            setSpreadCycle(0, true); steppers[0]->setSpeed(200);
            waitForSensor(0, LOW, 5000);
            if (abs(steppers[0]->currentPosition() % 3200) > 60) failed = true;
            else { lastGood = rpm; rpm += 10.0f; }
            setSpreadCycle(0, false);
        }
    }
    sys.cal[0].maxRpm = lastGood;
    saveCalibration(0);
    addLog("RESULT: " + String(lastGood,0) + " RPM");
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    addLog("Inertia M0...");
    float accel = 1000.0f; float lastGood = accel; bool failed = false;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 600.0f;

    while(accel <= 40000.0f && !failed) {
        addLog("Try Accel " + String(accel,0));
        steppers[0]->setMaxSpeed(rpmToSps(testSpd));
        steppers[0]->setAcceleration(accel);
        long base = steppers[0]->currentPosition();
        steppers[0]->moveTo(base + 3200);
        while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }
        steppers[0]->moveTo(base - 3200);
        while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }
        steppers[0]->moveTo(base);
        while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }

        setSpreadCycle(0, true); steppers[0]->setSpeed(200);
        waitForSensor(0, LOW, 5000);
        if (abs(steppers[0]->currentPosition() % 3200) > 60) failed = true;
        else { lastGood = accel; accel += 2000.0f; }
        setSpreadCycle(0, false);
    }
    sys.cal[0].maxAccel = lastGood;
    saveCalibration(0);
    addLog("RESULT: " + String(lastGood,0) + " Accel");
}

void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) steppers[0]->run();
}
