#include "Hal_Tacho.h"
#include "Hal_Pins.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"

namespace HalTacho {

TachoState tacho[4];

// FALLING-Trigger: ISR feuert nur bei HIGH→LOW. Spart das digitalRead in der ISR
// und vermeidet das Risiko dass die ISR auf der falschen Flanke kommt und
// digitalRead bereits umgeschaltet hat.

template<uint8_t I>
static void IRAM_ATTR tachoIsr() {
    tacho[I].pulseCount++;
    unsigned long now = millis();
    if (tacho[I].lastLowMs > 0) {
        unsigned long p = now - tacho[I].lastLowMs;
        if (p >= NOISE_FILTER_MS) tacho[I].periodMs = p;
    }
    tacho[I].lastLowMs = now;
    tacho[I].latch = true;
}

// Wird aus main_v4.cpp NACH allen anderen Inits aufgerufen (extra Pull-up-Setup,
// falls FAS oder PCNT den Pin durch Spätinitialisierung umkonfiguriert hat).
void reattach() {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        if (!HalPins::hasSensor(i)) continue;
        uint8_t pin = HalPins::MOTORS[i].tachoPin;
        detachInterrupt(digitalPinToInterrupt(pin));
        if (HalPins::MOTORS[i].tachoNeedsExtPullup) {
            pinMode(pin, INPUT);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }
        int n = digitalPinToInterrupt(pin);
        if (n < 0) {
            Logger::addLog(String("TACHO M") + (char)('X'+i) + ": pin " + pin + " kein Interrupt!");
            continue;
        }
        switch (i) {
            case 0: attachInterrupt(n, tachoIsr<0>, FALLING); break;
            case 1: attachInterrupt(n, tachoIsr<1>, FALLING); break;
            case 2: attachInterrupt(n, tachoIsr<2>, FALLING); break;
        }
        Logger::addLog(String("TACHO M") + (char)('X'+i) + ": pin " + pin + " ISR=FALLING ok");
    }
}

void init() {
    reattach();
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
