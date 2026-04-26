#pragma once
#include <Arduino.h>

// L4 — Sensor-Kalibrierung pro Motor (X/Y/Z)
//
// 4-Phasen-Logik aus v3 SensorCalib.cpp (Golden Standard v3.7.25):
//   P1: Grob-Suche CW (1000 sps) bis Sensor-Eintritt LOW
//   P2: Austritt finden CW (500 sps) bis HIGH
//   P3: 3-Touch rechte Kante A2 (CCW, 100 sps)
//   P4: 3-Touch linke Kante A1 (CW, 100 sps)
//   Final: center = (a1+a2)/2, NVS-Save, moveTo(center), setPositionDeg(0)

namespace Calibration {

void run(uint8_t motorIdx);

} // namespace Calibration
