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

// --- INTERRUPT LOGIC ---
volatile long lastSensorPos = -1;
volatile bool sensorHit = false;

void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) {
        lastSensorPos = steppers[0]->currentPosition();
        sensorHit = true;
    } else {
        sensorHit = false;
    }
}

// --- TELEMETRY CACHE ---
struct TelemCache {
    uint16_t sg = 0;
    uint8_t cs = 0;
    bool ola = false;
    bool olb = false;
    bool stall = false;
} tCache;

String telemCSV = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,stall,ola,olb\n";
    telemStart = millis();
    lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES) return;
    if (now - lastTelemMs < 50) return;
    lastTelemMs = now;

    long pos = steppers[0]->currentPosition();
    int  spd = (int)steppers[0]->speed();

    char line[100];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d\n",
        now - telemStart, phase, val, pos, spd, tCache.sg, tCache.cs, 
        tCache.stall, tCache.ola, tCache.olb);
    telemCSV += line;
}

void updateTelemCache() {
    tCache.sg = driverX.SG_RESULT();
    tCache.cs = driverX.cs_actual();
    uint32_t drv_status = driverX.DRV_STATUS();
    tCache.stall = (drv_status & 0x1);
    tCache.ola = (drv_status >> 30) & 0x1;
    tCache.olb = (drv_status >> 31) & 0x1;
}

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

void saveCalibration(int i) {
    prefs.begin("cal", false);
    String key = "m" + String(i);
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("cal", true);
    for (int i = 0; i < 4; i++) {
        String key = "m" + String(i);
        if (prefs.isKey(key.c_str())) prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    }
    prefs.end();
}

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        driverX.begin(); driverX.toff(5); driverX.rms_current(800); driverX.microsteps(16);
        digitalWrite(ENABLE_PIN, LOW);
        addLog("M0 ON (800mA)");
    } else {
        digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

void initMotors() {
    loadCalibration();
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    pinMode(ENABLE_PIN, OUTPUT); digitalWrite(ENABLE_PIN, LOW);
    driverX.begin(); driverX.toff(5); driverX.rms_current(800); driverX.microsteps(16);
    driverX.en_spreadCycle(true);
    
    pinMode(TACHO_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, CHANGE);
    
    steppers[0]->setMaxSpeed(4000); steppers[0]->setAcceleration(2000);
}

void homeMotor(int i) {
    if (i != 0) return;
    addLog("Homing M0...");
    lastSensorPos = -1;
    steppers[0]->setSpeed(1200);
    while (lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    
    steppers[0]->setSpeed(-400);
    while (digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    
    steppers[0]->setCurrentPosition(0);
    steppers[0]->moveTo(sys.cal[0].triggerCenter);
    while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    addLog("M0 Home.");
}

void characterizeSensor(int i) {
    if (i != 0) return;
    addLog("Precise Mapping M0 (ISR)...");
    driverX.en_spreadCycle(true);
    
    // CW Approach
    lastSensorPos = -1;
    steppers[0]->setSpeed(150);
    while(lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    long sCW = lastSensorPos;
    while(digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    long eCW = steppers[0]->currentPosition();
    
    // CCW (Move away and return)
    steppers[0]->move(-2000); while(steppers[0]->distanceToGo() != 0) { steppers[0]->run(); yield(); }
    lastSensorPos = -1;
    steppers[0]->setSpeed(-150);
    while(lastSensorPos == -1) { steppers[0]->runSpeed(); yield(); }
    long eCCW = lastSensorPos;
    while(digitalRead(TACHO_PIN) == LOW) { steppers[0]->runSpeed(); yield(); }
    long sCCW = steppers[0]->currentPosition();

    sys.cal[0].triggerCenter = ((sCW + eCW) / 2 + (sCCW + eCCW) / 2) / 2;
    sys.cal[0].valid = true;
    saveCalibration(0);
    addLog("M0 Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

void runSpeedTest(int i) {
    if (i != 0) return;
    addLog("Speed Test 3.3.3...");
    clearTelemetry();
    float rpm = 300.0f;
    driverX.en_spreadCycle(true);
    
    while(rpm <= 800.0f) {
        addLog("Testing " + String(rpm,0) + " RPM");
        steppers[0]->setMaxSpeed(rpmToSps(rpm));
        steppers[0]->setAcceleration(10000);
        steppers[0]->move(6400);
        
        unsigned long lastCache = 0;
        lastSensorPos = -1; // Reset for drift check
        
        while(steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            recordTelemetry("SPEED", rpm);
            if (millis() - lastCache > 500) { updateTelemCache(); lastCache = millis(); }
            yield();
        }
        
        // Use the last captured interrupt position for precision
        if (lastSensorPos != -1) {
            long drift = abs(lastSensorPos % 3200);
            addLog("Drift: " + String(drift) + " steps");
        }
        
        rpm += 100.0f;
    }
    addLog("Test Done.");
}

void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) steppers[0]->run();
}
