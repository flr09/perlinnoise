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

void applyDefaults(uint8_t motorIdx, uint16_t runMA = 800, uint16_t ms = 64);
void setCurrent(uint8_t motorIdx, uint16_t runMA, uint16_t holdMA);
uint16_t getRunCurrent(uint8_t motorIdx);
void setPower(uint8_t motorIdx, bool on);
bool isPowered(uint8_t motorIdx);
void setAllPower(bool on);  // gemeinsamer ENABLE-Pin schaltet alle gleichzeitig

// v4.3.1: TPWMTHRS-Schwelle für StealthChop ↔ SpreadCycle Übergang.
// 0       = SpreadCycle immer (laut, volles Drehmoment)
// 0xFFFFF = StealthChop immer (silent, weniger Drehmoment)
// dazwischen: bei TSTEP < threshold läuft SpreadCycle (= über RPM-Schwelle).
// Zur RPM-Konvertierung: Units::rpmToTpwmthrs(motorIdx, rpm).
void setTPWMTHRS(uint8_t motorIdx, uint32_t threshold);

// Backwards-compat aus Phase 1 — werden in Phase 3 entfernt
void applyDefaultsX(uint16_t runMA = 800);
void setPowerX(bool on);
bool isPoweredX();

} // namespace Tmc
