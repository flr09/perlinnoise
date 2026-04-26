#pragma once
#include <Arduino.h>
#include "../L0_platform/Types.h"

// L6 — State-Machine + pending-Flags + Emergency-Stop
//
// Zentrale Stelle für:
//  - aktuellen Operationszustand (idle/homing/calib/learn/test/show)
//  - Pending-Aktion-Flags (welcher Motor soll was tun)
//  - pendingStop (in alle Blocking-Loops eingebaut)
//
// Kapselt die in v3 verstreuten Einzelvariablen. Höhere Schichten lesen/setzen
// pending-Flags, MovementTask (in Phase 2 noch in main_v4.cpp) konsumiert sie.

namespace Op {

extern volatile v4::OpState state;
extern volatile bool        pendingStop;

// Pro-Motor Pending-Flags. -1 = nichts, sonst Motor-Index 0..3.
struct PendingFlags {
    volatile int home    = -1;
    volatile int calib   = -1;
    volatile int learn   = -1;
    volatile int test    = -1;
    volatile int show    = -1;
    volatile int setzero = -1;
    volatile int power   = -1;     // 0..3 Motor; eigentlicher Zielzustand: powerOn[idx]
    volatile bool powerOn[4] = { false, false, false, false };
    // Für test: welches Programm. 0=speed, 1=inertia, 2=coast, 3=katapult, 4=freq, 5=currentsweep, 6=perfshow
    volatile int testProg = 0;
};

extern PendingFlags pending;

void requestPower(uint8_t motorIdx, bool on);
void requestSetZero(uint8_t motorIdx);
void requestHome(uint8_t motorIdx);
void requestCalib(uint8_t motorIdx);
void requestStop();

bool isBusy();         // state != IDLE
const char* stateStr();

} // namespace Op
