#include "MotorControl.h"
#include "Programs.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = NULL;

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    if (sys.log.length() > 6000) sys.log = sys.log.substring(3000);
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

void saveCalibration(int i) {
    prefs.begin("cal", false);
    String key = "m" + String(i) + "_stable";
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("cal", true);
    for (int i = 0; i < 4; i++) {
        String key = "m" + String(i) + "_stable";
        if (prefs.getBytesLength(key.c_str()) == sizeof(CalibrationData)) {
            prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
            if (sys.cal[i].nvsVersion != 3619) sys.cal[i].valid = false;
        } else {
            sys.cal[i].valid = false;
        }
    }
    prefs.end();
}

void initMotors() {
    loadCalibration();
    uartMutex = xSemaphoreCreateMutex();
    initSensor();
    initDriver();
}

// v3.7.11 Logic: Fast 2-Touch Edge detection using ISR snapshot
long touchEdge(bool state, int dir, uint32_t speed) {
    long sum = 0;
    for (int n = 0; n < 2; n++) {
        stepper->setSpeedInHz(400);
        if (dir > 0) stepper->move(-(long)(stepsPerRev * 0.1f));
        else         stepper->move((long)(stepsPerRev * 0.1f));
        while (stepper->isRunning()) { if (sys.pendingStop) return -1; yield(); }
        delay(100);

        stepper->setSpeedInHz(speed);
        if (dir > 0) stepper->runForward();
        else         stepper->runBackward();
        
        sensorLatchReset(); // Arm the ISR snapshot
        if (!waitForSensorStable(state, (long)(stepsPerRev * 0.2f), 5000)) {
            stepper->stopMove(); return -1;
        }
        sum += lastSensorRaw; // Use high-precision ISR snapshot
        stepper->stopMove(); while (stepper->isRunning()) yield();
        delay(100);
    }
    return sum / 2;
}

void homeMotor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Homing (v3.7.11 Fast)...");
    stepper->setAcceleration(2000);
    
    sensorLatchReset(); 
    stepper->setSpeedInHz(800);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) {
        addLog("Err: Home fehlt");
        stepper->stopMove(); return;
    }
    long triggerPoint = lastSensorRaw;
    stepper->stopMove(); while (stepper->isRunning()) yield();

    if (sys.cal[0].valid) {
        long curPos = stepper->getCurrentPosition();
        long overshot = curPos - triggerPoint;
        // Sync encoder to the known trigger degree
        stepper->setCurrentPosition(degToSteps(sys.cal[0].triggerStartDeg) + overshot);
        
        // Drive to the logical center (0°)
        moveToDeg(0.0f);
        while (stepper->isRunning()) { if (sys.pendingStop) break; yield(); }
        
        if (!sys.pendingStop) {
            setPositionDeg(0.0f); // Final hard zero
            addLog("Home @0° OK.");
        }
    } else {
        stepper->setCurrentPosition(0);
        addLog("Home (uncal)");
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
}

void characterizeSensor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Calib v3.7.11 (Successive)...");
    stepper->setAcceleration(2000);

    // Ensure we are outside the sensor
    while (digitalRead(TACHO_PIN) == LOW) {
        stepper->setSpeedInHz(400); stepper->runBackward();
        if (!waitForSensorStable(HIGH, (long)stepsPerRev, 5000)) break;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();

    addLog("P1: Grob CW...");
    stepper->setSpeedInHz(1000);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) { addLog("Err: P1"); return; }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    
    addLog("P2: Austritt...");
    stepper->setSpeedInHz(500);
    stepper->runForward();
    if (!waitForSensorStable(HIGH, (long)(stepsPerRev * 0.5f), 10000)) { addLog("Err: P2"); return; }
    stepper->stopMove(); while (stepper->isRunning()) yield();

    addLog("P3: Touch A2 (rechts)...");
    long a2 = touchEdge(LOW, -1, 100);
    if (a2 == -1) { addLog("Err: P3"); return; }

    addLog("P4: Touch A1 (links)...");
    stepper->setSpeedInHz(500); stepper->runBackward();
    waitForSensorStable(HIGH, (long)stepsPerRev, 10000);
    stepper->stopMove(); while (stepper->isRunning()) yield();
    
    long a1 = touchEdge(LOW, 1, 100);
    if (a1 == -1) { addLog("Err: P4"); return; }

    long center = (a1 + a2) / 2;
    sys.cal[0].triggerStartDeg = stepsToDeg(a1 - center);
    sys.cal[0].triggerEndDeg   = stepsToDeg(a2 - center);
    sys.cal[0].valid = true;
    sys.cal[0].nvsVersion = 3619;
    saveCalibration(0);

    addLog("Mitte (0°) gesetzt.");
    stepper->setSpeedInHz(400);
    stepper->moveTo(center);
    while (stepper->isRunning()) yield();
    setPositionDeg(0.0f);

    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("Kalibrierung OK.");
}

void updateMotors() {
    if (sys.pendingStop && stepper) { stepper->stopMove(); sys.pendingStop = false; }
}
