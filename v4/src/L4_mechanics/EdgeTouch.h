#pragma once
#include <Arduino.h>

// L4 — präzises Antasten einer Sensorflanke
//
// Wiederverwendbare Primitive aus v3 SensorCalib.cpp (Golden Logic v3.7.25).
// `samples`-fach Antasten und Mittelung — eliminiert Hysterese und mechanische
// Varianzen. Rückgabe: Stepper-Position der Flanke in Steps. -1 bei Fehler/Stop.

namespace EdgeTouch {

long touch(uint8_t motorIdx, int targetSensorState, int dir,
           uint32_t speedSps, int samples);

} // namespace EdgeTouch
