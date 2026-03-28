#include "Watchdog.h"
#include "MotorProfile.h"
#include "Sensor.h"       // tachoPeriodMs, extern
#include "Telemetry.h"    // telemCacheSG, telemCacheCS
#include "Driver.h"       // stepsPerRev, spsToRpm, stepper
#include "MotorControl.h" // addLog, sys, motorMux
#include <math.h>

// --- ISR-Datenpuffer (IRAM-sicher) ---
volatile long     wdLastPos      = 0;
volatile long     wdDelta        = 0;
volatile uint32_t wdLastPeriodMs = 0;
volatile bool     wdNewPulse     = false;
volatile bool     wdFirstRev     = true;

// Interne Steuerung
static bool wdEnabled = false;  // Datenerfassung aktiv
static float wdLastArmedRpm = 0.0f;

void watchdogInit() {
    wdEnabled    = false;
    wdFirstRev   = true;
    wdNewPulse   = false;
    wdLastPos    = 0;
    wdDelta      = 0;
    wdLastPeriodMs = 0;
    sys.wd = WatchdogState(); // Reset Zustand
}

void watchdogEnable(bool enable) {
    portENTER_CRITICAL(&motorMux);
    wdEnabled  = enable;
    wdFirstRev = true;   // Immer neu starten — Baseline zurücksetzen
    wdNewPulse = false;
    portEXIT_CRITICAL(&motorMux);
    if (!enable) {
        sys.wd.active      = false;
        sys.wd.settleCount = 0;
        sys.wd.errorCount  = 0;
    }
}

// Läuft im Interrupt-Kontext — NUR volatile Variablen schreiben, kein malloc, kein Log
void IRAM_ATTR watchdogIsrUpdate(long currentPos, uint32_t periodMs) {
    if (!wdEnabled) return;
    if (!wdFirstRev) {
        wdDelta        = currentPos - wdLastPos;
        wdLastPeriodMs = periodMs;
        wdNewPulse     = true;
    }
    wdFirstRev = false;
    wdLastPos  = currentPos;
}

bool watchdogUpdate() {
    if (!wdEnabled || !wdNewPulse) return false;

    // Daten atomar aus ISR-Puffer lesen
    long     delta    = 0;
    uint32_t periodMs = 0;
    portENTER_CRITICAL(&motorMux);
    delta    = wdDelta;
    periodMs = wdLastPeriodMs;
    wdNewPulse = false;
    portEXIT_CRITICAL(&motorMux);

    // Aktuelle Drehzahl (kommandiert)
    float rpm = 0.0f;
    if (stepper) rpm = spsToRpm((float)(stepper->getCurrentSpeedInMilliHz() / 1000));
    if (rpm < 200.0f) {
        // Unterhalb Watchdog-Bereich: Settlezähler zurücksetzen, nicht urteilen
        sys.wd.settleCount = 0;
        return false;
    }

    // --- Drehzahländerung erkennen: Watchdog entsperren ---
    if (sys.wd.active && fabsf(rpm - wdLastArmedRpm) > wdLastArmedRpm * 0.08f) {
        sys.wd.active      = false;
        sys.wd.settleCount = 0;
        sys.wd.errorCount  = 0;
        wdLastArmedRpm = 0.0f;
    }

    // === Evidence A: Tacho-Periode vs. MotorProfile ===
    bool evidA = false;
    if (motorProfile.valid) {
        float pMean = 0.0f, pSigma = 0.0f;
        if (profileGetExpected(rpm, &pMean, &pSigma)) {
            float deviation = fabsf((float)periodMs - pMean);
            evidA = (deviation > 3.0f * pSigma);
        }
    }

    // === Evidence B: Schrittdelta vs. stepsPerRev ===
    bool evidB = false;
    long expectedSteps = (long)stepsPerRev;
    if (expectedSteps > 0) {
        long deviation = abs(abs(delta) - expectedSteps);
        evidB = (deviation > expectedSteps / 10); // >10% Abweichung
    }

    // === Evidence C: SG_RESULT unter Threshold ===
    bool evidC = (sys.cal[0].sgThrs > 0 && telemCacheSG() < sys.cal[0].sgThrs);

    int faultCount = (int)evidA + (int)evidB + (int)evidC;
    uint8_t faultCode = ((uint8_t)evidA << 2) | ((uint8_t)evidB << 1) | (uint8_t)evidC;

    // --- Zustand aktualisieren ---
    sys.wd.lastDelta    = delta;
    sys.wd.lastPeriodMs = periodMs;

    if (faultCount >= 2) {
        // Nur zählen wenn Watchdog scharf — vor Schärfung keine Fault-Auslösung
        if (sys.wd.active) {
            sys.wd.errorCount++;
            sys.wd.lastFaultCode = faultCode;
            sys.wd.settleCount   = 0;

            if (sys.wd.errorCount >= 3) {
                // 3 aufeinanderfolgende fehlerhafte Umdrehungen → FAULT
                sys.wd.triggered = true;
                sys.wd.active    = false;
                sys.pendingStop  = true;
                addLog("WD FAULT 0b" + String(faultCode, BIN) +
                       " d=" + String(delta) + "/" + String(expectedSteps) +
                       " p=" + String(periodMs) + "ms" +
                       " sg=" + String(telemCacheSG()));
                return true;
            }
        }
        sys.wd.settleCount = 0; // Settle-Zähler immer zurück bei Fehler
    } else {
        // Gute Umdrehung: Fehlerzähler zurück, Settle hochzählen
        sys.wd.errorCount = 0;
        sys.wd.settleCount++;

        if (!sys.wd.active && sys.wd.settleCount >= 5) {
            // 5 stabile Umdrehungen → Watchdog scharf
            sys.wd.active  = true;
            sys.wd.triggered = false;
            wdLastArmedRpm = rpm;
            addLog("WD armed @" + String((int)rpm) + "rpm");
        }
    }
    return false;
}
