#pragma once
#include "MotorControl.h"

// Test-Funktion: A1 via ISR-Latch, A2 via inline PCNT-Read nach digitalRead-Bestätigung.
// Kein NVS-Save — nur Messung + Vergleich mit gespeicherter Kalibrierung.
void characterizeSensorTest(int i);
