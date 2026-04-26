#pragma once
#include <Arduino.h>

// L1 — Sensor-Polling
//
// Stabiles Lesen des Tacho-Pins gegen EMI-Rauschen. v3-Erkenntnis:
// 5-Hit-Filter mit 50 µs Pause zwischen Reads ist robust auf GPIO 15
// trotz TMC2209-Switching-Noise.

namespace HalSensor {

// Liest den Pin und prüft, ob `targetState` `requiredHits`-mal in Folge stabil
// vorliegt. Max. 50 Versuche à `delayUs` Mikrosekunden Pause.
bool checkStable(uint8_t pin, int targetState, int requiredHits = 5, int delayUs = 50);

// Wartet bis Pin den Zielzustand hat oder Timeout/Stop. `motorIdx` wird für
// die Stop-Condition geprüft (sys.pendingStop pro Motor — kommt mit L6).
// In Phase-2-Minimalversion: globaler pendingStop reicht.
bool waitForStable(uint8_t pin, int targetState,
                   long stepperStartPos, long maxStepDelta,
                   unsigned long timeoutMs);

} // namespace HalSensor
