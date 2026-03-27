#include "Sensor.h"
#include <FastAccelStepper.h>
#include <Bounce2.h>
#include "Types.h"

extern portMUX_TYPE   motorMux;
extern FastAccelStepper* stepper;
extern SystemState sys;
void addLog(String msg);

// --- GLOBALS ---
volatile long     lastSensorRaw  = 0;
volatile bool     sensorHit      = false;
long              pcntStepperBase = 0;

volatile unsigned long tachoPeriodMs  = 0;
volatile unsigned long lastTachoLowMs = 0;
volatile uint32_t      pulseCount     = 0;

Bounce tachoDebouncer = Bounce();

// ISR nur für Tacho (Drehzahlmessung)
void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) {
        pulseCount++;
        unsigned long now = millis();
        if (lastTachoLowMs > 0) {
            unsigned long p = now - lastTachoLowMs;
            if (p >= 10) tachoPeriodMs = p;
        }
        lastTachoLowMs = now;
    }
}

// Robuste Status-Prüfung: Muss N-mal hintereinander den Zielzustand liefern
bool checkSensorStable(bool targetState, int requiredHits) {
    int hits = 0;
    for (int i = 0; i < 50; i++) { // Max 50 Versuche
        if (digitalRead(TACHO_PIN) == targetState) hits++;
        else hits = 0;
        if (hits >= requiredHits) return true;
        delayMicroseconds(50);
    }
    return false;
}

void sensorLatchReset() { sensorHit = false; }
void sensorLatchEnable(bool enable) { (void)enable; }

uint16_t getTachoRpm() {
    unsigned long period, lastT;
    portENTER_CRITICAL(&motorMux);
    period = tachoPeriodMs;
    lastT  = lastTachoLowMs;
    portEXIT_CRITICAL(&motorMux);
    if (period == 0 || (millis() - lastT > 2000)) return 0;
    return (uint16_t)min(9999UL, 60000UL / period);
}

uint32_t getPulseCount() {
    uint32_t c; portENTER_CRITICAL(&motorMux); c = pulseCount; portEXIT_CRITICAL(&motorMux);
    return c;
}

// Angepasst für 5-Hits Logik
bool waitForSensorStable(bool state, long maxSteps, unsigned long timeoutMs) {
    if (!stepper) return false;
    unsigned long start = millis();
    long startPos = stepper->getCurrentPosition();
    while (true) {
        if (checkSensorStable(state, 5)) return true;
        if (sys.pendingStop || millis() - start > timeoutMs || abs(stepper->getCurrentPosition() - startPos) > maxSteps) return false;
        yield();
    }
}

// Legacy wrapper
bool waitForSensorTimed(bool state, long maxSteps, unsigned long timeoutMs) {
    return waitForSensorStable(state, maxSteps, timeoutMs);
}

void initSensor() {
    pinMode(TACHO_PIN, INPUT_PULLUP);
    tachoDebouncer.attach(TACHO_PIN);
    tachoDebouncer.interval(10);
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, CHANGE);
}

void enablePcntInputBuffer() {}
