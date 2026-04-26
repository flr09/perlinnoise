#include "Hal_Sensor.h"

namespace HalSensor {

bool checkStable(uint8_t pin, int targetState, int requiredHits, int delayUs) {
    int hits = 0;
    for (int i = 0; i < 50; i++) {
        if (digitalRead(pin) == targetState) hits++;
        else hits = 0;
        if (hits >= requiredHits) return true;
        delayMicroseconds(delayUs);
    }
    return false;
}

// Stepper-abhängige Variante kommt in Phase 2 zusammen mit L3 Stepper.
// Hier nur eine Variante ohne Stepper-Querverweis (Layer-Hygiene): Caller
// muss Stepper-Position selbst tracken und über `stepperStartPos` +
// `maxStepDelta` die Bewegungsschranke einsetzen.
bool waitForStable(uint8_t pin, int targetState,
                   long stepperStartPos, long maxStepDelta,
                   unsigned long timeoutMs) {
    // Diese Funktion braucht eigentlich Stepper-Pos zum Vergleich. Da L1 aber
    // nicht auf L3 zugreifen darf, exponieren wir nur die Sensor-seitige
    // Logik. Caller (L4) ruft checkStable() in einer eigenen Schleife mit
    // eigenem Stepper-Check.
    //
    // Fallback ohne Stepper-Reference: nur Timeout-basiert.
    (void)stepperStartPos;
    (void)maxStepDelta;
    unsigned long start = millis();
    while (true) {
        if (checkStable(pin, targetState)) return true;
        if (millis() - start > timeoutMs) return false;
        yield();
    }
}

} // namespace HalSensor
