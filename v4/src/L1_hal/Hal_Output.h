#pragma once
#include <Arduino.h>

// L1 — HAL für die MOSFET-Ausgänge (Lüfter PWM, Lampe discrete).
//
// FYSETC E4 BED-MOSFET (GPIO 13) als PWM-Ausgang für den Lüfter,
// HOTEND-MOSFET (GPIO 2) als rein binärer Schaltausgang für die Lampe
// (externer Treiber dimmt selbst, daher hier nur on/off).

namespace HalOutput {

void init();

// 0..255 PWM-Duty
void setFan(uint8_t val);

void setLamp(bool on);

} // namespace HalOutput
