#pragma once
#include <Arduino.h>
#include <TMCStepper.h>

// L3 — TMC2209-Wrapper
//
// In Phase 1 nur Motor X aktiv konfiguriert. Die anderen 3 Treiber-Instanzen
// werden später dazu kommen (Multi-Motor in Phase 2). Pro Motor: rms_current,
// microsteps, SGTHRS, TPWMTHRS, TCOOLTHRS, en_spreadCycle, pwm_autoscale.
//
// Alle UART-Aktionen laufen über Sync::uartMutex (FreeRTOS-Semaphore), weil
// 1–10 ms pro Kommando.

namespace Tmc {

constexpr float R_SENSE = 0.11f;

void init();   // Initialisiert UART-Bus + Motor X. Y/Z/E later.
bool ready();  // true wenn init erfolgreich war

void applyDefaultsX(uint16_t runMA = 800);

// Power-Toggle für Motor X via TMC `toff`. ENABLE-Pin wird parallel gesteuert.
void setPowerX(bool on);
bool isPoweredX();

} // namespace Tmc
