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

// --- 6) FreqSweep: Linearer Chirp 10–200 Hz, Schwingung um SENSOR-KANTE ---
// Durch das Schwingen um die Kante (triggerStartDeg) statt um die Mitte
// wird bei JEDER Amplitude (auch < 1°) ein Tacho-Puls erzeugt, solange der
// Motor die Schritte hält. Das erlaubt eine präzise Analyse des
// Frequenzgangs bis in den hohen Bereich.
void runFreqSweep(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("FreqSweep: nicht kalibriert"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 64);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog("Freq Sweep (um Sensor-KANTE schwingend)");

    // Auf die Start-Kante fahren (ca. -15°)
    float edgeDeg = cal.triggerStartDeg;
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, edgeDeg);
    if (!waitOrStop(i, 5000)) { resetMotorState(i, false); return; }
    long edgePos = s->getCurrentPosition();

    unsigned long tStart = millis();
    float tDur = FREQ_SWEEP_S * 1000.0f;
    int dir = 1;
    uint32_t pSweepStart = HalTacho::getPulseCount(i);
    
    while (!Op::pendingStop) {
        float elapsed  = (float)(millis() - tStart);
        float progress = elapsed / tDur;
        if (progress >= 1.0f) break;

        float f = FREQ_MIN_HZ + (FREQ_MAX_HZ - FREQ_MIN_HZ) * progress;
        // Amplitude nimmt mit 1/f^2 ab.
        float ampF = (float)FREQ_ACCEL_MAX / (16.0f * f * f);
        long  amp  = (long)min(ampF * 0.9f, (float)Stepper::stepsPerRev(i) / 8.0f);
        
        if (amp < 2) break; 

        s->setSpeedInHz((uint32_t)sqrtf((float)FREQ_ACCEL_MAX * (float)amp));
        s->setAcceleration(FREQ_ACCEL_MAX);
        
        // Schwinge um die exakte Kante
        s->moveTo(edgePos + (dir * amp));
        
        while (s->isRunning()) {
            if (Op::pendingStop) { s->stopMove(); break; }
            Telemetry::recordDataPoint(i, "FS", f);
            vTaskDelay(pdMS_TO_TICKS(2)); // Höhere Telemetrie-Auflösung beim Sweep
        }
        dir = -dir;
    }
    
    uint32_t pTotal = HalTacho::getPulseCount(i) - pSweepStart;
    Logger::addLog(String("FreqSweep fertig. Impulse: ") + pTotal);
    
    // Wenn 0 Impulse bei FreqSweep (obwohl wir an der Kante schwingen), 
    // dann war es ein Stall oder mechanisches Problem.
    resetMotorState(i, (pTotal == 0));
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
