#pragma once
#include <Arduino.h>
#include "Types.h"

// --- Tacho-Watchdog: 2-aus-3-Überwachung pro Umdrehung ---
//
// Architektur:
//   ISR (IRAM, Core 1):  tachoISR → watchdogIsrUpdate()
//                        Erfasst Schrittdelta + Periode pro Umdrehung
//
//   Task (Core 0):       watchdogUpdate() — alle 50ms aufrufen
//                        3 Evidenzen prüfen, 2/3 → FAULT
//
// Einschalten:   watchdogEnable(true)
// Automatische Schärfung: nach 5 stabilen Umdrehungen innerhalb Profil-Fenster
// Automatische Entsperrung: bei Drehzahländerung > 5%, Stopp, Neustart
//
// Evidence A — Tacho-Periode vs. MotorProfile-Fenster (mean ± 3σ)
// Evidence B — Schrittdelta vs. stepsPerRev (±10%)
// Evidence C — SG_RESULT < sgThrs (bereits in Telemetry)

// --- ISR-Daten (volatile, schreibt ISR / liest Core-0-Task) ---
extern volatile long     wdLastPos;        // Stepperposition beim letzten Tacho-Trigger
extern volatile long     wdDelta;          // Schritte zwischen letzten zwei Triggern (signed)
extern volatile uint32_t wdLastPeriodMs;   // Tacho-Periode beim letzten Trigger (ms)
extern volatile bool     wdNewPulse;       // Flag: neue ISR-Daten verfügbar
extern volatile bool     wdFirstRev;       // true = noch kein Baseline (erste Umdrehung)

// --- Initialisierung (einmalig in setup/initMotors) ---
void watchdogInit();

// --- Aktivieren / Deaktivieren (von Motor-Task, Core 1) ---
// enable=true:  Datenerfassung ein, Schärfung läuft automatisch nach 5 stabilen Revs
// enable=false: Deaktiviert + Zustand zurückgesetzt (z.B. bei Stop, Drehzahlwechsel)
void watchdogEnable(bool enable);

// --- ISR-Hook (IRAM_ATTR, aufgerufen aus tachoISR bei jeder LOW-Flanke) ---
void IRAM_ATTR watchdogIsrUpdate(long currentPos, uint32_t periodMs);

// --- Auswertung (aufgerufen aus Core-0-Task nach jedem wdNewPulse) ---
// Gibt true zurück wenn FAULT ausgelöst (pendingStop wurde gesetzt)
bool watchdogUpdate();
