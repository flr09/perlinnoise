#pragma once
#include <Arduino.h>

// L4 — Homing pro Motor (X/Y/Z)
//
// Schnellsuche + 1-Touch-Bestätigung + Direkt-Anfahrt 0° (v3.7.25 Logik).
// Setzt voraus, dass Calibration gelaufen ist (sonst Notfall-Modus: Kante = 0).

namespace Homing {

void run(uint8_t motorIdx);

} // namespace Homing
