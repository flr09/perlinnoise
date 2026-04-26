#pragma once
#include <Arduino.h>

// L1 — Tacho-ISR pro Motor
//
// Pro Motor mit Sensor (X/Y/Z, nicht E): CHANGE-Interrupt auf TACHO_PIN,
// LOW-Flanke = 1 Umdrehung. Periode zwischen LOW-Flanken in `tachoPeriodMs`.
// Noise-Filter: nur updaten wenn Periode ≥ TACHO_NOISE_FILTER_MS (10 ms).
//
// Stand v3: ISR-Latch (`sensorHit`) unterscheidet erste Flanke von Bounces.
// In v4 ist das die gleiche Logik, nur pro-Motor instanziiert.

namespace HalTacho {

constexpr unsigned long NOISE_FILTER_MS = 10;
constexpr unsigned long STALE_TIMEOUT_MS = 2000;

struct TachoState {
    volatile unsigned long periodMs    = 0;
    volatile unsigned long lastLowMs   = 0;
    volatile uint32_t      pulseCount  = 0;
    volatile bool          latch       = false;  // ISR-Latch für Cal/Home
    volatile long          latchPos    = 0;      // Stepper-Pos bei Latch-Flanke
};

extern TachoState tacho[4];

void init();   // attachInterrupt für jeden vorhandenen Tacho-Pin (X/Y/Z)
uint16_t getRpm(uint8_t motorIdx);
uint32_t getPulseCount(uint8_t motorIdx);

void resetLatch(uint8_t motorIdx);
bool latchFired(uint8_t motorIdx);
long latchPosition(uint8_t motorIdx);

} // namespace HalTacho
