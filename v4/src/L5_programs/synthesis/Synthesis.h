#pragma once
#include <Arduino.h>

// L5b — Bewegungs-Synthese-Pipeline (kontinuierlich)
//
// init():    SimplexNoise-Engine + Stepper-Defaults vorbereiten
// tick():    1 Update-Schritt (im MovementTask-Loop alle 10ms aufrufen)
// start():   running=true, Power an, Microsteps auf 16
// stop():    running=false, Motoren stoppen

namespace Synthesis {

void init();
void start();
void stop();
void tick();

// Preview-Sample für L7-Visualisierung. Liest aktuellen Engine-State
// (flightX/Y, timeAcc, alle v4::rt-Parameter) ohne Seiteneffekt.
// Layout abhängig von v4::rt.moveType:
//   moveType 0..2 (Noise): 32x32 = 1024 Bytes, Grauwerte 0..255
//   moveType 3..5 (Wave):  128 Bytes, eine Periode 0..255
//   moveType 6   (STEP):   0 (kein Preview)
// Returns: Anzahl geschriebener Bytes (0 wenn Mode nicht visualisierbar).
size_t getPreviewBytes(uint8_t* out, size_t maxBytes);

} // namespace Synthesis
