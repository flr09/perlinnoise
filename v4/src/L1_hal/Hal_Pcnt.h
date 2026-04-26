#pragma once
#include <Arduino.h>
#include <driver/pcnt.h>

// L1 — PCNT (Pulse Counter) Hardware-Wrapper
//
// ESP32 hat 8 PCNT-Units (0..7). Wir nutzen Unit i für Motor i (max 4).
// PCNT zählt STEP-Pulse mit Richtungserkennung über DIR-Pin.
//
// Initialisierungsreihenfolge ist kritisch (siehe v3 Bug F6):
// PCNT-Setup MUSS vor FastAccelStepper-Init laufen, weil pcnt_unit_config()
// gpio_output_disable() aufruft und damit RMT-Routing zerstören würde.
// Daher wird init() vor Stepper::init() im setup() aufgerufen.
//
// Zusätzlich (v3 Fix F15): Input-Buffer auf STEP-Pins per IO_MUX FUN_IE
// einschalten, damit PCNT die Pulses überhaupt sieht. Wird in initInputBuffers()
// nach Stepper-Init aufgerufen.

namespace HalPcnt {

void init();                          // PCNT-Units für alle 4 Motoren konfigurieren
void initInputBuffers();              // FUN_IE auf STEP-Pins einschalten (nach FAS-Init)
int16_t read(uint8_t motorIdx);       // aktueller PCNT-Counter-Wert (signed)
void clear(uint8_t motorIdx);         // PCNT auf 0 setzen
void pause(uint8_t motorIdx);
void resume(uint8_t motorIdx);

// Absolute Position seit letztem clear() relativ zu base
struct PcntState {
    long base = 0;  // Stepper-Position bei letztem clear()
};
extern PcntState state[4];

} // namespace HalPcnt
