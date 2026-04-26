#pragma once
#include <Arduino.h>
#include <TMCStepper.h>

// L3 — TMC2209-Wrapper für 4 Motoren
//
// Alle 4 TMC2209 hängen am gleichen UART-Bus (Serial2, Pins 21/22), aber jeder
// hat eine eigene Slave-Adresse (laut FYSETC-Belegung): X=1, Y=3, Z=0, E=2.
// Alle UART-Aktionen über Sync::uartMutex (FreeRTOS-Semaphore).

namespace Tmc {

constexpr float R_SENSE = 0.11f;

void init();   // alle 4 Treiber + UART-Bus + ENABLE-Pin
bool ready();  // global init ok?
TMC2209Stepper* driver(uint8_t motorIdx);  // null wenn nicht init oder out-of-range

void applyDefaults(uint8_t motorIdx, uint16_t runMA = 800);
void setPower(uint8_t motorIdx, bool on);
bool isPowered(uint8_t motorIdx);
void setAllPower(bool on);  // gemeinsamer ENABLE-Pin schaltet alle gleichzeitig

// Backwards-compat aus Phase 1 — werden in Phase 3 entfernt
void applyDefaultsX(uint16_t runMA = 800);
void setPowerX(bool on);
bool isPoweredX();

} // namespace Tmc
