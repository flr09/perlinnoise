#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// L0 — Synchronisations-Primitives, von allen darüberliegenden Layern nutzbar.
//
// motorMux: Spinlock für sehr kurze Critical Sections (State-Reads/Writes).
//           Nicht für UART-Aktionen — die dauern Millisekunden, da blockiert
//           Spinlock zu lange. Spinlock ist Core-übergreifend sicher.
//
// uartMutex: FreeRTOS-Semaphore für TMC2209-UART. Kommandos brauchen 1–10 ms,
//            Mutex erlaubt Context-Switch während des Wartens.

namespace Sync {

extern portMUX_TYPE motorMux;
extern SemaphoreHandle_t uartMutex;
// Bug 77 (v4.4.24): Logger hat eigenen Mutex, sonst Contention mit TMC-UART
// (Bug-74-Fix migrierte Logger fälschlich auf uartMutex). Logger holdet bis
// zu 50 ms für Buffer-Copy (6 KB) → blockiert TMC-Operationen. Eigener
// Mutex koppelt Logger von TMC-UART ab.
extern SemaphoreHandle_t loggerMutex;

void init();

} // namespace Sync
