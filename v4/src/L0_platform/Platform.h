#pragma once
#include <Arduino.h>

// L0 — Plattform-Bootstrap
//
// init():            Serial + Sync + Logger
// startMovementTask: pinned zu Core 1 (App-CPU). Erfordert MovementTaskFn.
//
// Die Movement-Task-Funktion wird von höheren Layern (L4/L5) bereitgestellt
// und hier nur gepinned + gestartet. So bleibt L0 frei von Domain-Wissen.

namespace Platform {

using MovementTaskFn = void (*)(void*);

constexpr const char* FW_VERSION = "4.4.21";

void init();
void startMovementTask(MovementTaskFn fn, const char* name = "MovementTask",
                       uint32_t stackBytes = 10000, UBaseType_t prio = 1);

} // namespace Platform
