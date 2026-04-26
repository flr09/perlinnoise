// --- CalibTest.cpp ---
// Test-Implementierung für A2-Erfassung ohne ISR-Latch-Reset zwischen A1 und A2.
//
// Ansatz:
//   A1 → ISR-Latch (erste LOW-Flanke, wie bisher)
//   A2 → Motor fährt weiter bis Sensorfeld verlassen (HIGH),
//         dann PCNT direkt inline lesen — kein sensorLatchReset nötig.
//
// Vorteil:
//   Kein fixes Schrittfenster, kein Bounce2, kein Timing-Hack.
//   A2-Position wird genau dann gelesen wenn digitalRead(TACHO_PIN) == HIGH bestätigt.
//
// Aktivierung: HTTP /cmd?a=calibtest
// Deklaration in CalibTest.h, Registrierung in main_v3.cpp pending.
//
#include "MotorControl.h"
#include "driver/pcnt.h"

// Inline PCNT read (gleiche Methode wie ISR — IRAM-safe, aber hier im Task-Kontext)
static inline long readPcntAbs() {
    return (long)(int16_t)(PCNT.cnt_unit[PCNT_UNIT_0].val & 0xFFFF) + pcntStepperBase;
}

void characterizeSensorTest(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("[TEST] Calib-Test (ISR A1 / inline A2)...");
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.en_spreadCycle(true); xSemaphoreGive(uartMutex);
    }
    stepper->setAcceleration(2000);

    // --- Vorbereitung: links vom Sensor starten ---
    if (digitalRead(TACHO_PIN) == LOW) {
        stepper->setSpeedInHz(500); stepper->runForward();
        if (!waitForSensorTimed(HIGH, (long)(stepsPerRev * 0.5f), 5000)) {
            addLog("[TEST] Err: Prep CW Exit Timeout");
            stepper->stopMove(); while (stepper->isRunning()) { yield(); } return;
        }
        stepper->stopMove(); while (stepper->isRunning()) { yield(); }
    }
    stepper->setSpeedInHz(500); stepper->runBackward();
    if (!waitForSensorTimed(HIGH, (long)(stepsPerRev * 0.3f), 5000)) {
        addLog("[TEST] Err: Prep CCW Timeout");
        stepper->stopMove(); while (stepper->isRunning()) { yield(); } return;
    }
    stepper->stopMove(); while (stepper->isRunning()) { yield(); }
    delay(2);

    // PCNT-Sync
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    pcntStepperBase = stepper->getCurrentPosition();
    delay(2);

    // --- Phase 1: A1 per ISR-Latch (Eintritt LOW) ---
    addLog("[TEST] Suche A1...");
    sensorLatchReset();
    stepper->setSpeedInHz(500);
    stepper->runForward();
    if (!waitForSensorTimed(LOW, (long)(stepsPerRev * 1.5f), 20000)) {
        addLog("[TEST] Err: A1 nicht gefunden");
        stepper->stopMove(); while (stepper->isRunning()) { yield(); } return;
    }
    long a1 = (long)lastSensorRaw + pcntStepperBase;
    addLog("[TEST] A1=" + String(a1) + " (lastRaw=" + String(lastSensorRaw) + ")");

    // --- Phase 2: A2 inline — warten bis Sensorfeld verlassen (HIGH), dann PCNT direkt lesen ---
    // Kein sensorLatchReset hier. ISR bleibt wie er ist (A1-Latch aktiv aber irrelevant).
    // digitalRead bestätigt stabile HIGH-Flanke → PCNT inline = A2-Position.
    addLog("[TEST] Suche A2 (inline PCNT)...");
    if (!waitForSensorTimed(HIGH, (long)(stepsPerRev * 0.5f), 10000)) {
        addLog("[TEST] Err: A2 nicht gefunden");
        stepper->stopMove(); while (stepper->isRunning()) { yield(); } return;
    }
    long a2 = readPcntAbs();
    stepper->stopMove(); while (stepper->isRunning()) { yield(); }
    delay(2);

    addLog("[TEST] A2=" + String(a2) + "  Delta=" + String(a2 - a1) + " steps");

    if (a2 <= a1) {
        addLog("[TEST] Err: A2<=A1! delta=" + String(a2 - a1));
        return;
    }

    long centerSteps = (long)lroundf((float)(a1 + a2) / 2.0f);
    float startDeg   = stepsToDeg(a1 - centerSteps);
    float endDeg     = stepsToDeg(a2 - centerSteps);

    addLog("[TEST] Start=" + String(startDeg, 2) + "° End=" + String(endDeg, 2)
           + "° Width=" + String(endDeg - startDeg, 2) + "°");

    // Vergleich mit gespeicherter Kalibrierung (falls vorhanden)
    if (sys.cal[0].valid) {
        addLog("[TEST] Cal stored: Start=" + String(sys.cal[0].triggerStartDeg, 2)
               + "° End=" + String(sys.cal[0].triggerEndDeg, 2) + "°");
        float diffStart = startDeg - sys.cal[0].triggerStartDeg;
        float diffEnd   = endDeg   - sys.cal[0].triggerEndDeg;
        addLog("[TEST] Abweichung: dStart=" + String(diffStart, 2)
               + "° dEnd=" + String(diffEnd, 2) + "°");
    }

    // Zur Mitte fahren und 0° setzen (wie echte Calib, aber OHNE NVS-Save)
    stepper->setSpeedInHz(400);
    stepper->moveTo(centerSteps);
    while (stepper->isRunning()) { yield(); }
    setPositionDeg(0.0f);
    setMicrosteps(64);
    applyDriverSettings(sys.cal[0].learnedCurrentMA > 0
                        ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
    addLog("[TEST] Fertig — 0° gesetzt (kein NVS-Save).");
}
