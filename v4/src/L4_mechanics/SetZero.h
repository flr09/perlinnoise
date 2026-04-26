#pragma once
#include <Arduino.h>

// L4 — SetZero: aktuelle Position als 0° setzen (manuelle Nullung).
//
// Funktioniert für alle 4 Motoren — auch E (open-loop, einziger Weg dort).

namespace SetZero {

void apply(uint8_t motorIdx);

} // namespace SetZero
