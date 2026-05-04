#include "Characterization.h"
#include "../../L0_platform/Logger.h"
#include "../../L0_platform/Types.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L1_hal/Hal_Tacho.h"
#include "../../L2_storage/Storage_Calib.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L3_driver/Motion.h"
#include "../../L3_driver/Units.h"
#include "../../L4_mechanics/Calibration.h"
#include "../../L4_mechanics/Homing.h"
#include "../../L6_telemetry_safety/OpState.h"
#include "../../L6_telemetry_safety/Telemetry.h"

namespace Characterization {

// --- Konstanten ---
constexpr uint16_t PARCOUR_RPM_MAX  = 2500;
constexpr uint16_t PARCOUR_RPM_STEP = 100;

constexpr float    FREQ_MIN_HZ      = 10.0f;
constexpr float    FREQ_MAX_HZ      = 200.0f;
constexpr float    FREQ_SWEEP_S     = 15.0f;
constexpr uint32_t FREQ_ACCEL_MAX   = 500000UL;
constexpr long     FREQ_AMP_MIN     = 5;

// Pancake Soft-Limit (motors/pancake/datasheet.md): Nennstrom 920mA RMS,
// Soft-Limit 900mA, Empfohlen 650-850mA. NIEMALS überschreiten — sonst Hitze.
constexpr uint16_t MOTOR_CURRENT_HARD_MAX = 900;

// Stall-Detection: Pulses-pro-Sekunde realer Wert vs. erwarteter Wert.
// < 0.7 = klare Schritt-Verluste, < 0.5 = Stall.
constexpr float STALL_RATIO_THR  = 0.5f;
constexpr float WARN_RATIO_THR   = 0.7f;

// --- Hilfsfunktionen ---

static bool waitOrStop(uint8_t i, unsigned long timeoutMs = 30000) {
    return Motion::waitWhileRunning(i, &Op::pendingStop, timeoutMs);
}

// Misst über sampleMs die Tacho-Puls-Rate und vergleicht gegen die erwartete
// Pulsrate (1 Puls = 1 Umdrehung). Rückgabe: ratio = real/expected.
//
// Voraussetzung: Motor muss durch die Sensor-Zunge fahren während der Messung,
// sonst gibt's keine Pulse — also nur sinnvoll bei kontinuierlicher Drehung
// ODER bei Bewegungen die explizit am Sensor vorbei führen.
static float measureMotorRatio(uint8_t i, float expectedRpm, unsigned long sampleMs) {
    uint32_t p0 = HalTacho::getPulseCount(i);
    unsigned long t0 = millis();
    while (millis() - t0 < sampleMs) {
        if (Op::pendingStop) return 1.0f;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t p1 = HalTacho::getPulseCount(i);
    float realPps     = (float)(p1 - p0) * 1000.0f / (float)sampleMs;
    float expectedPps = expectedRpm / 60.0f;
    if (expectedPps < 0.5f) return 1.0f;  // unter ~30 RPM nicht aussagekräftig
    return realPps / expectedPps;
}

// Defensives Cleanup nach jedem Test — auch im Stop-Pfad. Setzt Microsteps
// auf 64 zurück, applyDefaults mit gelerntem Strom, führt bei Stall ein
// Re-Homing durch um die absolute Position für Folgetests zu retten.
static void resetMotorState(uint8_t i, bool wasStalled) {
    auto* s = Stepper::get(i);
    if (s && s->isRunning()) {
        s->stopMove();
        Motion::waitWhileRunning(i, nullptr, 5000);
    }
    Stepper::setMicrosteps(i, 64);
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    uint16_t restoreCurrent = cal.learnedCurrentMA > 0
        ? min(cal.learnedCurrentMA, MOTOR_CURRENT_HARD_MAX)
        : 800;
    Tmc::applyDefaults(i, restoreCurrent);

    if (wasStalled && cal.valid) {
        Logger::addLog("Stall erkannt -> Re-Homing...");
        Homing::run(i);
    } else if (cal.valid && s) {
        // Normales Reset zur Mitte (0°) ohne volle Homing-Suche
        long pos = s->getCurrentPosition();
        long spr = (long)Stepper::stepsPerRev(i);
        long modPos = ((pos % spr) + spr) % spr;
        if (modPos > spr / 2) modPos -= spr;
        s->setCurrentPosition(modPos);
        s->setSpeedInHz(8000);
        s->setAcceleration(20000);
        Motion::moveToDeg(i, 0.0f);
        Motion::waitWhileRunning(i, &Op::pendingStop, 5000);
    }
}

// --- 1) SG-Learn: TMC StallGuard-Threshold lernen ---
// Fixed RPM-Stufen, dazwischen sauber stoppen, Tacho-Cross-Check pro Stufe.
// SG-Sampling erst NACH Settle-Zeit (200ms), nicht während Hochlauf.
void runSgLearn(uint8_t i) {
    if (i >= 4) return;
    if (!HalPins::hasSensor(i)) {
        Logger::addLog(String("M") + (char)('X'+i) + ": SG-Learn — kein Sensor (kein Tacho-Cross-Check)");
    }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    s->setAcceleration(30000);
    Logger::addLog(String("M") + (char)('X'+i) + ": SG-Learn");

    float testRpms[] = {150, 250, 350, 450};
    float sgSum = 0; int samples = 0;
    bool ok = true;
    for (int k = 0; k < 4 && !Op::pendingStop; k++) {
        s->setSpeedInHz((uint32_t)Units::rpmToSps(i, testRpms[k]));
        s->runForward();
        // Settle-Zeit für Hochlauf (Acc=30k → ~200ms für 450 RPM-Sprung)
        unsigned long t0 = millis();
        while (millis() - t0 < 250) { if (Op::pendingStop) break; vTaskDelay(pdMS_TO_TICKS(10)); }

        // Tacho-Cross-Check: läuft Motor wirklich bei testRpm?
        if (HalPins::hasSensor(i)) {
            float ratio = measureMotorRatio(i, testRpms[k], 1000);
            if (ratio < WARN_RATIO_THR) {
                Logger::addLog(String("SG-Learn FAIL @") + (int)testRpms[k] + " RPM (ratio=" + String(ratio,2) + ")");
                ok = false; break;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        // SG-Werte nur in Settle-Phase (200ms) sammeln
        unsigned long t1 = millis();
        while (millis() - t1 < 500 && !Op::pendingStop) {
            sgSum += (float)Telemetry::getLatestSg(i);
            samples++;
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        // Sauberer Stop zwischen RPM-Stufen — kein direkter Speed-Sprung
        s->stopMove();
        if (!waitOrStop(i, 2000)) { ok = false; break; }
    }

    if (ok && samples > 0) {
        v4::CalibrationData cal;
        StorageCalib::load(i, cal);
        cal.sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
        StorageCalib::save(i, cal);
        Logger::addLog(String("SG-Thrs: ") + cal.sgThrs);
    } else if (!ok) {
        Logger::addLog("SG-Learn: abgebrochen, kein Wert gespeichert");
    }
    resetMotorState(i, !ok);
}

// --- 2) SpeedTest: RPM-Rampe bis Tacho-Stall ---
// Acc adaptiv: bei höheren RPMs braucht's mehr Hochlauf-Zeit.
// Stall-Detection via measureMotorRatio (1s Sample) — robuster als getRpm().
void runSpeedTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("SpeedTest: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("SpeedTest: kein Sensor → keine Stall-Detection"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog(String("M") + (char)('X'+i) + ": Speed Test (Tacho-referenziert)");

    float rpm = 300.0f;
    float lastGood = 0.0f;
    bool stalled = false;
    while (rpm <= PARCOUR_RPM_MAX && !Op::pendingStop) {
        // Acc skaliert mit Ziel-RPM: für 2500 RPM → ~150k sps² (=Hochlauf in 0.9s)
        uint32_t targetSps = (uint32_t)Units::rpmToSps(i, rpm);
        uint32_t accel     = max(30000U, targetSps * 2);  // mindestens 30k
        s->setAcceleration(accel);
        s->setSpeedInHz(targetSps);
        s->runForward();

        // Settle: Hochlauf-Zeit + 200ms Buffer
        unsigned long settle = (unsigned long)(targetSps * 1000.0f / accel) + 200;
        unsigned long t0 = millis();
        while (millis() - t0 < settle) {
            if (Op::pendingStop) break;
            Telemetry::recordDataPoint(i, "SPEED_SETTLE", rpm);
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        // Stall-Messung: 1s Sample bei stabiler Drehzahl
        float ratio = measureMotorRatio(i, rpm, 1000);
        Logger::addLog(String("SPD ") + (int)rpm + " sps=" + targetSps + " acc=" + accel + " ratio=" + String(ratio,2));
        Telemetry::recordDataPoint(i, "SPEED_RATIO", rpm);

        if (ratio < STALL_RATIO_THR) {
            Logger::addLog(String("STALL @") + (int)rpm + " RPM");
            stalled = true;
            break;
        }
        if (ratio >= WARN_RATIO_THR) lastGood = rpm;
        rpm += PARCOUR_RPM_STEP;
    }
    if (lastGood > 0) {
        cal.maxRpm = lastGood;
        StorageCalib::save(i, cal);
        Logger::addLog(String("Speed maxRpm = ") + (int)lastGood);
    }
    resetMotorState(i, stalled);
}

// --- 3) InertiaTest: Beschleunigung bis Tacho-Stall ---
// Bewegung bewusst DURCH die Sensor-Zunge: ±1 Umdrehung um 0° (Zunge=0°
// dank Cal). Pro Hin-/Rück-Bewegung wird der Sensor 2× passiert.
// Wenn Pulses pro Bewegungs-Zyklus deutlich < 4 → Stall.
void runInertiaTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("Inertia: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("Inertia: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog(String("M") + (char)('X'+i) + ": Inertia Test (sensor-referenziert)");

    // Vor Test: Motor auf 0° (Sensor-Mitte) bringen — danach passiert jede
    // Hin-Rück-Bewegung um ±1 rev die Zunge zwingend 2× pro Richtung.
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, 0.0f);
    if (!waitOrStop(i, 5000)) { resetMotorState(i, true); return; }

    float testSpd = cal.maxRpm > 0 ? cal.maxRpm * 0.7f : 400.0f;
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, testSpd));

    // Acc-Sweep geometrisch: 5k → 10k → 20k → ... → 500k. Feiner als linear,
    // findet Sweet-Spot deutlich präziser.
    uint32_t accs[] = {5000, 10000, 20000, 40000, 80000, 150000, 250000, 400000, 500000};
    uint32_t lastGood = 0;
    bool stalled = false;
    for (uint8_t k = 0; k < sizeof(accs)/sizeof(accs[0]) && !Op::pendingStop; k++) {
        uint32_t acc = accs[k];
        s->setAcceleration(acc);

        // Bewegung 2 rev hin + 2 rev rück → 2 Sensor-Durchgänge pro Hälfte.
        // Robuster als 1 rev: bei einem verfehlten Polling-Event ist immer
        // noch ein zweiter Eintritt da. Bei echtem Stall bleiben deltas = 0.
        long dist = 2 * (long)Stepper::stepsPerRev(i);
        uint32_t pBefore = HalTacho::getPulseCount(i);
        s->move(+dist);
        if (!waitOrStop(i, 8000)) break;
        uint32_t pMid = HalTacho::getPulseCount(i);
        s->move(-dist);
        if (!waitOrStop(i, 8000)) break;
        uint32_t pAfter = HalTacho::getPulseCount(i);
        uint32_t deltaHin  = pMid - pBefore;
        uint32_t deltaRueck= pAfter - pMid;

        bool ok = (deltaHin >= 1 && deltaRueck >= 1);
        Logger::addLog(String("INERT acc=") + acc + " hin=" + deltaHin + " rueck=" + deltaRueck + (ok?" OK":" STALL"));
        Telemetry::recordDataPoint(i, "INERT_H", deltaHin);
        Telemetry::recordDataPoint(i, "INERT_R", deltaRueck);
        if (!ok) { stalled = true; break; }
        lastGood = acc;
    }
    if (lastGood > 0) {
        cal.maxAccel = lastGood;
        StorageCalib::save(i, cal);
        Logger::addLog(String("Inertia maxAccel = ") + lastGood);
    }
    resetMotorState(i, stalled);
}

// --- 4) CoastTest: Auslauf nach Strom-Aus, Tacho-Pulses zählen ---
// Wichtig: kein stopMove() vor Power-off, sonst elektrisch gebremst.
// Stattdessen Stepper-Position ist egal, wir wollen nur freie Auslauf-Pulses.
void runCoastTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!HalPins::hasSensor(i)) { Logger::addLog("Coast: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog("Coast Test");

    s->setAcceleration(30000);
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, 400));
    s->runForward();
    delay(1500);  // Hochlauf + Settle
    if (Op::pendingStop) { resetMotorState(i, true); return; }

    // Ohne stopMove(): nur Strom abschalten, Motor coastet frei
    Tmc::setPower(i, false);
    uint32_t p0 = HalTacho::getPulseCount(i);
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
        Telemetry::recordDataPoint(i, "COAST", 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (Op::pendingStop) break;
    }
    uint32_t p1 = HalTacho::getPulseCount(i);
    Logger::addLog(String("Coast pulses (3s): ") + (p1 - p0));

    // Power restaurieren
    Tmc::setPower(i, true);
    resetMotorState(i, true);
}

// --- 5) Katapult: 3× Anlauf bei maxRpm, mit Stall-Check ---
void runKatapult(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("Katapult: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("Katapult: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog("KATAPULT");

    float launchRpm  = cal.maxRpm > 1000 ? cal.maxRpm * 0.9f : 2000.0f;
    uint32_t launchAcc = cal.maxAccel > 50000 ? (uint32_t)cal.maxAccel : 100000;
    s->setAcceleration(launchAcc);
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, launchRpm));

    bool stalled = false;
    for (int r = 0; r < 3 && !Op::pendingStop; r++) {
        s->runForward();
        unsigned long settle = (unsigned long)(Units::rpmToSps(i, launchRpm) * 1000.0f / launchAcc) + 100;
        delay(settle);
        float ratio = measureMotorRatio(i, launchRpm, 800);
        Logger::addLog(String("KAT r=") + r + " ratio=" + String(ratio,2));
        Telemetry::recordDataPoint(i, "KAT", launchRpm);
        if (ratio < STALL_RATIO_THR) { Logger::addLog("KAT: Stall"); stalled = true; break; }
        s->stopMove();
        if (!waitOrStop(i, 5000)) break;
        delay(200);
    }
    resetMotorState(i, stalled);
}

// --- 6) FreqSweep v2: 10 log-spaced Bänder, Bisektion bis Stall, Learning ---
//
// Pro Frequenzband wird die maximale Amplitude bestimmt, bei der der Motor
// noch nicht stallt — direkt durch Anfahren der Stall-Grenze. Ergebnis pro
// Band wird in NVS (cal.freqStallAmp[band]) gespeichert.
//
// Alle FREQ_FULL_EVERY Runs läuft die volle Bisektion (mehrere Stalls + Re-
// Homes pro Band). Dazwischen Learning-Pfad: 0.85× und 1.15× der gespeicherten
// Amplitude prüfen — wenn beide Erwartungen passen, leichter Nudge nach oben.
constexpr uint8_t FREQ_BANDS      = 10;
constexpr float   FREQ_BAND_HZ[FREQ_BANDS] = {
    10.0f, 14.0f, 19.0f, 26.0f, 36.0f, 50.0f, 69.0f, 96.0f, 132.0f, 200.0f
};
constexpr uint8_t FREQ_FULL_EVERY = 5;
constexpr float   FREQ_LEARN_LO   = 0.85f;
constexpr float   FREQ_LEARN_HI   = 1.15f;
constexpr float   FREQ_NUDGE_UP   = 1.02f;
constexpr int     FS_TEST_MS      = 200;
constexpr int     FS_MIN_SWINGS   = 4;

// Test ein (f, amp)-Punkt. Schwingt N Halbschwingungen um edgePos, zählt
// Tacho-Pulse. Stall = pulses < swings/2.
static bool fsTestPoint(uint8_t i, float f, long amp, long edgePos, int& dir) {
    auto* s = Stepper::get(i);
    if (!s || amp < FREQ_AMP_MIN) return true;
    float halfPeriodMs = 500.0f / f;  // 1/(2f) in ms
    int swings = (int)max((float)FS_MIN_SWINGS, (float)FS_TEST_MS / halfPeriodMs);

    // Speed: peak-Geschwindigkeit für Halbschwingung amp in 1/(2f) sec.
    // Sinusförmiges Profil → peak ≈ π·amp·f, hier konservativ 4·amp·f (S-Kurve).
    uint32_t targetSps = (uint32_t)max(100.0f, 4.0f * (float)amp * f);
    s->setAcceleration(FREQ_ACCEL_MAX);
    s->setSpeedInHz(targetSps);

    uint32_t p0 = HalTacho::getPulseCount(i);
    for (int n = 0; n < swings; n++) {
        if (Op::pendingStop) { s->stopMove(); return true; }
        s->moveTo(edgePos + (dir * amp));
        unsigned long t0 = millis();
        unsigned long maxMs = (unsigned long)(halfPeriodMs * 3.0f) + 20;
        while (s->isRunning() && (millis() - t0) < maxMs) {
            if (Op::pendingStop) { s->stopMove(); return true; }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (s->isRunning()) { s->stopMove(); Motion::waitWhileRunning(i, nullptr, 100); }
        dir = -dir;
    }
    uint32_t pulses = HalTacho::getPulseCount(i) - p0;
    bool stall = pulses < (uint32_t)(swings / 2);
    Telemetry::recordDataPoint(i, "FS_TEST", f);
    Logger::addLog(String("FS f=") + (int)f + " amp=" + amp
        + " p=" + pulses + "/" + swings + (stall ? " STALL" : ""));
    return stall;
}

// Re-Home + zurück zur Edge-Position. Ruft Homing::run(), nutzt cal.triggerStartDeg
// als Edge-Referenz. Returns neue edgePos in steps.
static long fsRehomeToEdge(uint8_t i, float edgeDeg) {
    Homing::run(i);
    auto* s = Stepper::get(i);
    if (!s) return 0;
    // Homing endet bei MS=64, FreqSweep braucht aber MS=64 → passt.
    Stepper::setMicrosteps(i, 64);
    s->setAcceleration(20000);
    s->setSpeedInHz(8000);
    Motion::moveToDeg(i, edgeDeg);
    Motion::waitWhileRunning(i, &Op::pendingStop, 5000);
    return s->getCurrentPosition();
}

// Volle Bisektion für ein Band. Returns max stabile Amplitude (steps).
// Bei Stall innerhalb der Suche wird re-homed. outRehomed signalisiert ob
// danach noch ein finales Re-Home nötig ist.
static long fsBisect(uint8_t i, float f, long ampMax, float edgeDeg,
                     long& edgePos, int& dir) {
    long low  = FREQ_AMP_MIN;
    long high = ampMax;
    if (high <= low) return low;
    bool stallTop = fsTestPoint(i, f, high, edgePos, dir);
    if (!stallTop) {
        // Selbst bei ampMax kein Stall — Band lebt durch, max stabil = ampMax.
        return high;
    }
    edgePos = fsRehomeToEdge(i, edgeDeg);

    int iter = 0;
    long tol = max((long)2, ampMax / 50);
    while ((high - low) > tol && iter < 6 && !Op::pendingStop) {
        long mid = (low + high) / 2;
        bool stall = fsTestPoint(i, f, mid, edgePos, dir);
        if (stall) {
            high = mid;
            edgePos = fsRehomeToEdge(i, edgeDeg);
        } else {
            low = mid;
        }
        iter++;
    }
    return low;
}

// Learning-Pfad: erwartet stored amp im NVS. Probe 0.85× (kein Stall erwartet)
// und 1.15× (Stall erwartet). Bei Erfolg Nudge ×1.02. Bei Mismatch Returns 0
// → Aufrufer fällt zurück auf Bisektion.
static long fsLearningVerify(uint8_t i, float f, long stored, long ampMax,
                             float edgeDeg, long& edgePos, int& dir) {
    if (stored <= 0) return 0;
    long lo = max((long)FREQ_AMP_MIN, (long)(stored * FREQ_LEARN_LO));
    long hi = min(ampMax, (long)(stored * FREQ_LEARN_HI));
    bool stallLo = fsTestPoint(i, f, lo, edgePos, dir);
    if (stallLo) {
        // Schon unter stored stallt → Lern-Wert war zu hoch, Re-Home + Bisekt.
        edgePos = fsRehomeToEdge(i, edgeDeg);
        return 0;
    }
    bool stallHi = fsTestPoint(i, f, hi, edgePos, dir);
    if (!stallHi) {
        // Über stored kein Stall → Lern-Wert war zu konservativ, Bisekt.
        return 0;
    }
    edgePos = fsRehomeToEdge(i, edgeDeg);
    long nudged = (long)(stored * FREQ_NUDGE_UP);
    return min(nudged, ampMax);
}

void runFreqSweep(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("FreqSweep: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("FreqSweep: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 64);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }

    bool fullSweep = (cal.freqRunCount % FREQ_FULL_EVERY) == 0;
    Logger::addLog(String("FreqSweep v2 (run #") + cal.freqRunCount
        + (fullSweep ? ", FULL bisect)" : ", LEARN verify)"));

    // Auf Sensor-Kante fahren — alle Tests schwingen um diesen Punkt.
    float edgeDeg = cal.triggerStartDeg;
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, edgeDeg);
    if (!waitOrStop(i, 5000)) { resetMotorState(i, false); return; }
    long edgePos = s->getCurrentPosition();
    int dir = 1;
    long sprQuarter = (long)Stepper::stepsPerRev(i) / 4;

    bool anyStall = false;
    for (uint8_t b = 0; b < FREQ_BANDS && !Op::pendingStop; b++) {
        float f = FREQ_BAND_HZ[b];
        // Physikalisches Maximum bei gegebener Acc-Grenze: amp ≤ acc/(16·f²)
        long ampPhysMax = (long)((float)FREQ_ACCEL_MAX / (16.0f * f * f));
        long ampMax = min(sprQuarter, max((long)FREQ_AMP_MIN, ampPhysMax));

        long result = 0;
        if (!fullSweep) {
            result = fsLearningVerify(i, f, (long)cal.freqStallAmp[b],
                                      ampMax, edgeDeg, edgePos, dir);
            if (result == 0) {
                Logger::addLog(String("FS band ") + b + ": verify miss → bisect");
                anyStall = true;
                result = fsBisect(i, f, ampMax, edgeDeg, edgePos, dir);
            }
        } else {
            result = fsBisect(i, f, ampMax, edgeDeg, edgePos, dir);
            anyStall = true;  // Full enthält i.d.R. Stalls
        }

        cal.freqStallAmp[b] = (uint16_t)max((long)0, min((long)0xFFFF, result));
        Logger::addLog(String("FS band ") + b + " f=" + (int)f
            + " stallAmp=" + cal.freqStallAmp[b]);
        Telemetry::recordDataPoint(i, "FS_BAND", f);
    }

    cal.freqRunCount = (uint8_t)((cal.freqRunCount + 1) % 100);
    StorageCalib::save(i, cal);
    Logger::addLog(String("FreqSweep v2 fertig (next run = ")
        + cal.freqRunCount + (((cal.freqRunCount % FREQ_FULL_EVERY) == 0) ? " FULL)" : " LEARN)"));

    resetMotorState(i, anyStall);
}

// --- 7) CurrentSweepHiRPM: B-EMF Sweet-Spot bei hoher RPM ---
// Strom-Sweep mit Tacho-Stall-Detection. Strom-Limit harter Cap (Datasheet).
void runCurrentSweepHiRPM(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("CurrentSweep: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("CurrentSweep: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog(String("CurrentSweep @ HiRPM (cap=") + MOTOR_CURRENT_HARD_MAX + "mA)");

    float rpm = cal.maxRpm > 1000 ? cal.maxRpm * 0.9f : 2200.0f;
    uint32_t targetSps = (uint32_t)Units::rpmToSps(i, rpm);
    uint32_t spinupAcc = max(50000U, targetSps * 2);
    s->setAcceleration(spinupAcc);
    s->setSpeedInHz(targetSps);
    s->runForward();
    unsigned long spinupMs = (unsigned long)(targetSps * 1000.0f / spinupAcc) + 200;
    delay(spinupMs);

    bool stalled = false;
    for (uint16_t mA = MOTOR_CURRENT_HARD_MAX; mA >= 200 && !Op::pendingStop; mA -= 100) {
        Tmc::applyDefaults(i, mA);
        unsigned long t0 = millis();
        while (millis() - t0 < 800) {
            Telemetry::recordDataPoint(i, "CS_HI", mA);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        float ratio = measureMotorRatio(i, rpm, 600);
        Logger::addLog(String("CS mA=") + mA + " ratio=" + String(ratio,2));
        if (ratio < STALL_RATIO_THR) {
            Logger::addLog(String("Sweet-Spot Floor: ") + (mA + 100) + "mA");
            stalled = true;
            break;
        }
    }
    resetMotorState(i, stalled);
}

// Performance Show: alle Tests in sinnvoller Reihenfolge.
// 1. SgLearn (Threshold lernen) → 2. SpeedTest (Top-RPM) → 3. InertiaTest
// (Top-Acc) → 4. Katapult (3 Bursts bei Top-RPM) → 5. CurrentSweep (Sweet-
// Spot) → 6. CoastTest (Auslauf) → 7. FreqSweep (Frequenzgang).
// Reihenfolge wichtig: SpeedTest+InertiaTest setzen Cal-Werte für die
// nachfolgenden Tests, die diese als Defaults brauchen.
void runPerformanceShow(uint8_t i) {
    Logger::addLog("Vorführung Start");

    Logger::addLog("Show 1/7: SG-Learn");
    runSgLearn(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 2/7: SpeedTest");
    runSpeedTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 3/7: Inertia");
    runInertiaTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 4/7: Katapult");
    runKatapult(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 5/7: CurrentSweep");
    runCurrentSweepHiRPM(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 6/7: Coast");
    runCoastTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 7/7: FreqSweep");
    runFreqSweep(i);

    Logger::addLog("Vorführung Ende");
}

} // namespace Characterization
