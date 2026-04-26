#pragma once
#include <Arduino.h>
#include <FastAccelStepper.h>

// L3 — FastAccelStepper-Wrapper für 4 Motoren
//
// Eine FAS-Engine, 4 FAS-Stepper-Instanzen. STEP/DIR/ENABLE-Pins kommen aus
// L1::HalPins. Microstep-Switch mit atomarer Position-Skalierung wie in v3.
//
// Reihenfolge im setup() (Bug F6 aus v3):
//   1) HalPcnt::init()       (PCNT Setup, ohne Input-Buffer)
//   2) Tmc::init()           (UART + ENABLE)
//   3) Stepper::init()       (FAS-Engine + 4 Stepper-Instanzen, RMT-Routing)
//   4) HalPcnt::initInputBuffers()  (Input-Buffer auf STEP-Pins, F15-Fix)

namespace Stepper {

void init();
FastAccelStepper* get(uint8_t motorIdx);

uint16_t microsteps(uint8_t motorIdx);
uint16_t stepsPerRev(uint8_t motorIdx);   // 200 * microsteps

void setMicrosteps(uint8_t motorIdx, uint16_t ms);

} // namespace Stepper
