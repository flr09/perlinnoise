#pragma once
#include <Arduino.h>

// L3 — High-level Bewegungs-API auf Grad-Basis.
//
// Alle Bewegungs-Befehle für L4/L5 gehen über diese API. Direkter
// Stepper-Zugriff aus L4 ist möglich, aber für Standard-Operationen sollte
// Motion verwendet werden — so überlebt späterer Microstep-Wechsel atomar.

namespace Motion {

void  moveToDeg(uint8_t motorIdx, float deg);
void  moveByDeg(uint8_t motorIdx, float deg);
void  setPositionDeg(uint8_t motorIdx, float deg);
float getPositionDeg(uint8_t motorIdx);

bool  isRunning(uint8_t motorIdx);

// Wartet bis Stepper steht ODER Stop ausgelöst wurde ODER Timeout.
// `pendingStopFlag` ist ein Pointer auf einen volatile bool — Caller
// steuert seinen eigenen Stop-Kanal (typischerweise OpState::pendingStop in L6).
bool  waitWhileRunning(uint8_t motorIdx, volatile bool* pendingStopFlag,
                       unsigned long timeoutMs = 30000);

} // namespace Motion
