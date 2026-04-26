#pragma once
#include <Arduino.h>
#include "Stepper.h"

// L3 — Einheiten-Konvertierung. Pure inline math, kein Hardware-Zugriff.
// Microstep-Wechsel ist atomic in Stepper::setMicrosteps — diese Funktionen
// holen sich `stepsPerRev` immer frisch.

namespace Units {

inline float rpmToSps(uint8_t motorIdx, float rpm) {
    return (rpm * (float)Stepper::stepsPerRev(motorIdx)) / 60.0f;
}

inline float spsToRpm(uint8_t motorIdx, float sps) {
    uint16_t spr = Stepper::stepsPerRev(motorIdx);
    return spr > 0 ? (sps * 60.0f) / (float)spr : 0.0f;
}

inline uint32_t rpmToTpwmthrs(uint8_t motorIdx, float rpm) {
    float usps = rpm * (float)Stepper::stepsPerRev(motorIdx) / 60.0f;
    return (usps < 1.0f) ? 0xFFFFF : (uint32_t)(12000000.0f / usps);
}

inline long degToSteps(uint8_t motorIdx, float deg) {
    return (long)lroundf(deg * (float)Stepper::stepsPerRev(motorIdx) / 360.0f);
}

inline float stepsToDeg(uint8_t motorIdx, long steps) {
    uint16_t spr = Stepper::stepsPerRev(motorIdx);
    return spr > 0 ? (float)steps * 360.0f / (float)spr : 0.0f;
}

} // namespace Units
