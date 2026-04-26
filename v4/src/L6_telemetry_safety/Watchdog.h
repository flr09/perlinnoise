#pragma once
#include <Arduino.h>

// L6 — Watchdog für Motor 0..3
//
// Drei-Beweis-Logik (B-EMF/Schritt-Verlust-Erkennung):
//   A) Tacho-Periode weicht > 3σ vom MotorProfile ab
//   B) Schritt-Delta weicht > 10% von stepsPerRev ab (eine Umdrehung)
//   C) SG_RESULT unter SG-Threshold (Cal-gelernt)
//
// >= 2 von 3 Bedingungen über 3 aufeinanderfolgende Umdrehungen → FAULT,
// pendingStop wird ausgelöst. Self-Arm nach 5 stabilen Umdrehungen.

namespace Watchdog {

struct State {
    bool     active        = false;
    bool     triggered     = false;
    uint8_t  lastFaultCode = 0;   // bit0=SG, bit1=StepDelta, bit2=Periode
    uint8_t  errorCount    = 0;
    uint8_t  settleCount   = 0;
    long     lastDelta     = 0;
    uint32_t lastPeriodMs  = 0;
};

extern State state[4];

void init();
void enable(uint8_t motorIdx, bool on);
void tick();   // wird zyklisch (z.B. 50ms) aufgerufen — startTask() erledigt das

void startTask();

} // namespace Watchdog
