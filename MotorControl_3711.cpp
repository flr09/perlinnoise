#include "MotorControl.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t uartMutex = NULL;

void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    if (sys.log.length() > 8000) sys.log = sys.log.substring(sys.log.length() - 4000);
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

// Hilfsfunktion für präzises Antasten (Mittelwert aus 3 Versuchen)
long touchEdge(bool state, int dir, uint32_t speed) {
    long sum = 0;
    for (int n = 0; n < 3; n++) {
        // Erst ein Stück wegfahren
        stepper->setSpeedInHz(400);
        if (dir > 0) stepper->move(-(long)(stepsPerRev * 0.1f));
        else         stepper->move((long)(stepsPerRev * 0.1f));
        while (stepper->isRunning()) yield();
        delay(100);

        // Dann langsam anfahren
        stepper->setSpeedInHz(speed);
        if (dir > 0) stepper->runForward();
        else         stepper->runBackward();
        
        if (!waitForSensorStable(state, (long)(stepsPerRev * 0.2f), 5000)) return -1;
        sum += stepper->getCurrentPosition();
        stepper->stopMove(); while (stepper->isRunning()) yield();
        delay(100);
    }
    return sum / 3;
}

void homeMotor(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Homing (Präzision)...");
    stepper->setAcceleration(2000);
    
    // Schnellsuche
    stepper->setSpeedInHz(800);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) {
        addLog("Err: Home nicht gefunden");
        stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();

    // Antasten
    long a1 = touchEdge(LOW, 1, 150);
    if (a1 == -1) { addLog("Err: Home-Touch"); return; }

    if (sys.cal[0].valid) {
        stepper->setCurrentPosition(degToSteps(sys.cal[0].triggerStartDeg));
        moveToDeg(0.0f);
        while (stepper->isRunning()) yield();
        setPositionDeg(0.0f);
        addLog("Home @0° OK");
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
    addLog("Calib v3.7.10 (Antasten)...");
    stepper->setAcceleration(2000);

    // Prep: Sicherstellen dass wir draußen sind
    while (digitalRead(TACHO_PIN) == LOW) {
        stepper->setSpeedInHz(400); stepper->runBackward();
        if (!waitForSensorStable(HIGH, (long)stepsPerRev, 5000)) break;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);

    // 1. Grob-Suche Eintritt (1000 sps CW)
    addLog("Phase 1: Grob CW...");
    stepper->setSpeedInHz(1000);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) {
        addLog("Err: Eintritt nicht gefunden");
        stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    
    // 2. Fenster-Suche Austritt (500 sps CW)
    addLog("Phase 2: Austritt CW...");
    stepper->setSpeedInHz(500);
    stepper->runForward();
    if (!waitForSensorStable(HIGH, (long)(stepsPerRev * 0.5f), 10000)) {
        addLog("Err: Austritt nicht gefunden");
        stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);

    // 3. Fein-Antasten A2 (100 sps CCW - von rechts kommend)
    addLog("Phase 3: Antasten A2 (rechts)...");
    long a2 = touchEdge(LOW, -1, 100);
    if (a2 == -1) { addLog("Err: A2 Touch"); return; }
    addLog("A2: " + String(a2));

    // 4. Fein-Antasten A1 (100 sps CW - von links kommend)
    addLog("Phase 4: Antasten A1 (links)...");
    // Erst ganz zurück fahren
    stepper->setSpeedInHz(500);
    stepper->runBackward();
    waitForSensorStable(HIGH, (long)stepsPerRev, 10000);
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);

    long a1 = touchEdge(LOW, 1, 100);
    if (a1 == -1) { addLog("Err: A1 Touch"); return; }
    addLog("A1: " + String(a1));

    // 5. Mitte setzen
    long center = (a1 + a2) / 2;
    sys.cal[0].triggerStartDeg = stepsToDeg(a1 - center);
    sys.cal[0].triggerEndDeg   = stepsToDeg(a2 - center);
    sys.cal[0].valid = true;
    sys.cal[0].nvsVersion = 3619;
    saveCalibration(0);

    addLog("Mitte: " + String(center));
    stepper->setSpeedInHz(400);
    stepper->moveTo(center);
    while (stepper->isRunning()) yield();
    setPositionDeg(0.0f);

    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("Kalibrierung OK.");
}

void learnSGProfile(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runSpeedTest(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runInertiaTest(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runCoastTest(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runKatapult(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runFreqSweep(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void runPerformanceShow(int i) { if (i==0) addLog("Skipped in v3.7.10"); }
void updateMotors() { if (sys.pendingStop && stepper) { stepper->stopMove(); sys.pendingStop = false; } }
void gotoCardinal() {}
