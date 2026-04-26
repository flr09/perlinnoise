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

// ISR-Versuch verworfen: GPIO 15 (Z-MIN) wirft auf dem FYSETC E4
// `E (64) gpio: gpio_isr_handler...` Fehler beim attachInterrupt — Strapping-Pin-
// Eigenheit. Stattdessen pollt Watchdog::tick() den Pin alle 50ms (Software-
// Edge-Detection). Reicht bis ~600 RPM, ausreichend für Cal/Home (<50 RPM).
//
// Falls später Multi-Motor-Sensoren auf GPIO 34/35 dazukommen, dort eventuell
// die ISR-Variante reaktivieren — diese Pins sind unkritisch.
void reattach() {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        if (!HalPins::hasSensor(i)) continue;
        uint8_t pin = HalPins::MOTORS[i].tachoPin;
        if (HalPins::MOTORS[i].tachoNeedsExtPullup) {
            pinMode(pin, INPUT);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }
        Logger::addLog(String("TACHO M") + (char)('X'+i) + ": pin " + pin + " (poll-mode)");
    }
    (void)tachoIsr<0>;  // unterdrückt unused-Warning
    (void)tachoIsr<1>;
    (void)tachoIsr<2>;
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
