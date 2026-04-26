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

} // namespace Synthesis
