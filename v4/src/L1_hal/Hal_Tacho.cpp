#include "Hal_Tacho.h"
#include "Hal_Pins.h"
#include "../L0_platform/Sync.h"

namespace HalTacho {

TachoState tacho[4];

// Pro-Motor-ISR: muss IRAM-resident sein. ESP32 attachInterrupt erlaubt nur
// freie Funktionen ohne Argumente — wir machen daher 4 dünne Wrapper.
//
// Stepper-Position wird hier NICHT direkt gelesen (würde in Phase 3 ergänzt,
// wenn Stepper-Modul verfügbar ist). Latch-Pos kommt vom Caller via setLatchPos.

template<uint8_t I>
static void IRAM_ATTR tachoIsr() {
    if (digitalRead(HalPins::MOTORS[I].tachoPin) == LOW) {
        tacho[I].pulseCount++;
        unsigned long now = millis();
        if (tacho[I].lastLowMs > 0) {
            unsigned long p = now - tacho[I].lastLowMs;
            if (p >= NOISE_FILTER_MS) tacho[I].periodMs = p;
        }
        tacho[I].lastLowMs = now;
        if (!tacho[I].latch) {
            tacho[I].latch = true;
            // latchPos wird vom Caller (L4) nach dem Latch-Erkennen gesetzt,
            // nicht in der ISR — Stepper-Position kann im ISR-Kontext nicht
            // sicher gelesen werden.
        }
    }
}

void init() {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        if (!HalPins::hasSensor(i)) continue;
        uint8_t pin = HalPins::MOTORS[i].tachoPin;
        if (HalPins::MOTORS[i].tachoNeedsExtPullup) {
            pinMode(pin, INPUT);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }
        switch (i) {
            case 0: attachInterrupt(digitalPinToInterrupt(pin), tachoIsr<0>, CHANGE); break;
            case 1: attachInterrupt(digitalPinToInterrupt(pin), tachoIsr<1>, CHANGE); break;
            case 2: attachInterrupt(digitalPinToInterrupt(pin), tachoIsr<2>, CHANGE); break;
            // i==3 (E) hat keinen Tacho — wird durch hasSensor()-Check übersprungen
        }
    }
}

uint16_t getRpm(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    unsigned long period, lastT;
    portENTER_CRITICAL(&Sync::motorMux);
    period = tacho[motorIdx].periodMs;
    lastT  = tacho[motorIdx].lastLowMs;
    portEXIT_CRITICAL(&Sync::motorMux);
    if (period == 0 || (millis() - lastT > STALE_TIMEOUT_MS)) return 0;
    return (uint16_t)min(9999UL, 60000UL / period);
}

uint32_t getPulseCount(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    uint32_t c;
    portENTER_CRITICAL(&Sync::motorMux);
    c = tacho[motorIdx].pulseCount;
    portEXIT_CRITICAL(&Sync::motorMux);
    return c;
}

void resetLatch(uint8_t motorIdx) {
    if (motorIdx >= 4) return;
    portENTER_CRITICAL(&Sync::motorMux);
    tacho[motorIdx].latch = false;
    portEXIT_CRITICAL(&Sync::motorMux);
}

bool latchFired(uint8_t motorIdx) {
    if (motorIdx >= 4) return false;
    return tacho[motorIdx].latch;
}

long latchPosition(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    return tacho[motorIdx].latchPos;
}

} // namespace HalTacho
