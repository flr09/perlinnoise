#include "SensorCalib.h"

// Externals aus dem Hauptsystem (MotorControl.cpp / Driver.cpp)
extern SystemState sys;
extern uint16_t stepsPerRev;
extern FastAccelStepper* stepper;
void addLog(String msg);
void saveCalibration(int i);
void setMotorPower(int i, bool on);
void setMicrosteps(uint16_t ms);
void applyDriverSettings(uint16_t runMA);

// Robustes Antasten einer Kante mit n Samples (Default=3 für Kalibrierung)
long touchSensorEdge(bool state, int dir, uint32_t speed, int samples) {
    long sum = 0;
    for (int n = 0; n < samples; n++) {
        // Stück wegfahren
        stepper->setSpeedInHz(400);
        if (dir > 0) stepper->move(-(long)(stepsPerRev * 0.1f));
        else         stepper->move((long)(stepsPerRev * 0.1f));
        while (stepper->isRunning()) { if (sys.pendingStop) return -1; yield(); }
        delay(100);

        // Kante anfahren
        stepper->setSpeedInHz(speed);
        if (dir > 0) stepper->runForward();
        else         stepper->runBackward();
        
        if (!waitForSensorStable(state, (long)(stepsPerRev * 0.2f), 5000)) {
            stepper->stopMove(); return -1;
        }
        sum += stepper->getCurrentPosition();
        stepper->stopMove(); while (stepper->isRunning()) yield();
        delay(100);
    }
    return sum / samples;
}

// Kalibrierung (Characterize): Benötigt 3 Samples für maximale Präzision (Golden Standard)
void runSensorCalibration(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("CALIB: v3.7.25 calib final");
    stepper->setAcceleration(2000);

    // Vorbereitung: Sensor verlassen
    while (digitalRead(TACHO_PIN) == LOW) {
        stepper->setSpeedInHz(400); stepper->runBackward();
        if (!waitForSensorStable(HIGH, (long)stepsPerRev, 5000)) break;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);

    // Phase 1: Grob CW Suche Eintritt
    addLog("CAL: P1 Grob CW...");
    stepper->setSpeedInHz(1000);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) {
        addLog("ERR: P1 Eintritt"); stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    
    // Phase 2: Austritt finden
    addLog("CAL: P2 Austritt...");
    stepper->setSpeedInHz(500);
    stepper->runForward();
    if (!waitForSensorStable(HIGH, (long)(stepsPerRev * 0.5f), 10000)) {
        addLog("ERR: P2 Austritt"); stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);

    // Phase 3: Präzisions-Antasten A2 (rechts) mit 3 Samples
    addLog("CAL: P3 Kante Rechts (3-Touch)...");
    long a2 = touchSensorEdge(LOW, -1, 100, 3);
    if (a2 == -1) { addLog("ERR: P3 Touch"); return; }

    // Phase 4: Präzisions-Antasten A1 (links) mit 3 Samples
    addLog("CAL: P4 Kante Links (3-Touch)...");
    stepper->setSpeedInHz(500); stepper->runBackward();
    waitForSensorStable(HIGH, (long)stepsPerRev, 10000);
    stepper->stopMove(); while (stepper->isRunning()) yield();
    delay(200);
    
    long a1 = touchSensorEdge(LOW, 1, 100, 3);
    if (a1 == -1) { addLog("ERR: P4 Touch"); return; }

    // Berechnungen & NVS Speicherung
    long center = (a1 + a2) / 2;
    sys.cal[0].triggerStartDeg = stepsToDeg(a1 - center);
    sys.cal[0].triggerEndDeg   = stepsToDeg(a2 - center);
    sys.cal[0].valid = true;
    sys.cal[0].nvsVersion = 3619;
    saveCalibration(0);

    // Auf Mitte fahren und Nullpunkt setzen
    addLog("CAL: Mitte gefunden. Fahre zu 0°...");
    stepper->setSpeedInHz(400);
    stepper->moveTo(center);
    while (stepper->isRunning()) yield();
    setPositionDeg(0.0f);

    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("CAL: Fertig.");
}

// Homing: Nutzt Kalibrierungs-Daten um schnell zu 0° zu kommen
void runMotorHoming(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("HOME: v3.7.25 Robust");
    stepper->setAcceleration(2000);
    
    // Schnellsuche
    stepper->setSpeedInHz(800);
    stepper->runForward();
    if (!waitForSensorStable(LOW, (long)(stepsPerRev * 2.0f), 15000)) {
        addLog("ERR: Home Sensor nicht gefunden");
        stepper->stopMove(); return;
    }
    stepper->stopMove(); while (stepper->isRunning()) yield();

    // Nur ein präziser Touch (1 Sample) reicht hier, um den aktuellen Kanten-Offset zu bestätigen
    addLog("HOME: Kante bestätigen...");
    long a1_current = touchSensorEdge(LOW, 1, 150, 1);
    if (a1_current == -1) { addLog("ERR: Home Kanten-Touch"); return; }

    if (sys.cal[0].valid) {
        // Den Encoder auf den bekannten Winkelwert der Kante (triggerStartDeg) synchronisieren
        stepper->setCurrentPosition(degToSteps(sys.cal[0].triggerStartDeg));
        
        // Jetzt auf 0° (Mitte) fahren
        moveToDeg(0.0f);
        while (stepper->isRunning()) { if (sys.pendingStop) break; yield(); }
        
        if (!sys.pendingStop) {
            setPositionDeg(0.0f); // Finaler Hard-Zero im Stillstand
            addLog("HOME: @0° OK.");
        }
    } else {
        // Falls nicht kalibriert: Setzen wir die Kante einfach als 0 (Notfall-Modus)
        stepper->setCurrentPosition(0);
        addLog("HOME: Fertig (uncal)");
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
}
