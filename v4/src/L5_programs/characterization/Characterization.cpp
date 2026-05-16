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
#include "../../L4_mechanics/EdgeTouch.h"
#include "../../L4_mechanics/Homing.h"
#include "../../L6_telemetry_safety/OpState.h"
#include "../../L6_telemetry_safety/Telemetry.h"
#include "../../L6_telemetry_safety/MotorProfile.h"

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
        Logger::addLog(String("M") + v4::motorName(i) + ": SG-Learn — kein Sensor (kein Tacho-Cross-Check)");
    }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    s->setAcceleration(30000);
    Logger::addLog(String("M") + v4::motorName(i) + ": SG-Learn");

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
    Logger::addLog(String("M") + v4::motorName(i) + ": Speed Test (Tacho-referenziert)");

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
    Logger::addLog(String("M") + v4::motorName(i) + ": Inertia Test (sensor-referenziert)");

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

// --- 6) FreqSweep v2 retake: 10 Bänder, Hysterese-Floor + Stall-Bisektion ---
//
// Lessons aus dem ersten v2-Versuch (4.1.3-rc1, zurückgerollt — Bug-IDs 21+22):
//   - Bug 22: amp < Sensor-Hysterese erzeugt 0 Pulse, der „pulses < swings/2"-
//     Detektor interpretierte das als Stall → Bisektion lief in den Floor.
//   - Bug 21: nach Stall scheiterte Re-Home (CW-Suche allein) → Folgetests an
//     Müll-Position. Dank ID 28 ist Re-Home jetzt CW+CCW robust.
//
// v4.2.2-Lösung pro Band:
//   1. Hysterese-Floor-Pre-Detection: kleinste amp finden bei der überhaupt
//      Pulse entstehen. 0-Pulse-Tests zählen NICHT als Stall, nur als „unter
//      der Wahrnehmungsschwelle". Falls floor > ampMaxPhys → Band überspringen.
//   2. Stall-Bisektion oberhalb floor bis zur physikalischen Acc-Grenze.
//      Stall = pulses == 0 BEI amp ≥ floor (also Motor schafft Bewegung
//      mechanisch nicht mehr). Re-Home nach jedem Stall.
//   3. Ergebnis pro Band in cal.freqStallAmp[band] (NVS-Schema 4003 schon
//      reserviert).
//
// Zeitbudget: ~60–90 s pro Motor (10 Bänder × ~5 Tests × 250 ms + Re-Homes).

// Frequenz-Raster für FreqSweep v2 retake (Tacho-only, da StallGuard bei
// oszillierender Bewegung unzuverlässig — Befund 2026-05-06):
// Engmaschig im 6..20 Hz Bereich, um die Hysterese-Schwelle der Sensorzunge
// genau zu lokalisieren (User-Anliegen: ist es 12 oder 18 Hz?). Darüber
// gröber 26..50 Hz — wird mit dieser Sensor-Mechanik vermutlich eh nicht
// erfasst, aber zur Bestätigung der Sweet-Spot-Lage geprüft.
constexpr uint8_t FREQ_BANDS_V2 = 10;
constexpr float   FREQ_BAND_HZ_V2[FREQ_BANDS_V2] = {
    6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 20.0f, 26.0f, 36.0f, 50.0f
};
constexpr int     FS2_TEST_MS    = 250;
constexpr int     FS2_MIN_SWINGS = 4;
constexpr long    FS2_AMP_MIN    = 1;

// Test ein (f, amp)-Punkt. Schwingt N Halbschwingungen um edgePos und liefert
// Pulse + max(SG_RESULT). Tacho-Pulse für Sensor-basierte Erkennung (oft 0
// wegen Sensor-Hysterese), SG_RESULT als sensor-unabhängige Last-Messung
// vom TMC2209. Aufrufer kann Stall klassifizieren über (sgMax == 0): Motor
// bewegt sich nicht erkennbar (entweder gestallt oder nicht angesteuert).
static void fs2TestPoint(uint8_t i, float f, long amp, long edgePos, int& dir,
                         int& outSwings, int& outPulses, uint16_t& outSgMax) {
    outSwings = 0;
    outPulses = 0;
    outSgMax  = 0;
    auto* s = Stepper::get(i);
    if (!s || amp < FS2_AMP_MIN) return;
    float halfPeriodMs = 500.0f / f;
    int swings = (int)max((float)FS2_MIN_SWINGS, (float)FS2_TEST_MS / halfPeriodMs);
    outSwings = swings;

    uint32_t targetSps = (uint32_t)max(100.0f, 4.0f * (float)amp * f);
    s->setAcceleration(FREQ_ACCEL_MAX);
    s->setSpeedInHz(targetSps);

    uint32_t p0 = HalTacho::getPulseCount(i);
    uint16_t sgMax = 0;
    for (int n = 0; n < swings; n++) {
        if (Op::pendingStop) { s->stopMove(); return; }
        s->moveTo(edgePos + (dir * amp));
        unsigned long t0 = millis();
        unsigned long maxMs = (unsigned long)(halfPeriodMs * 3.0f) + 20;
        while (s->isRunning() && (millis() - t0) < maxMs) {
            if (Op::pendingStop) { s->stopMove(); return; }
            // SG_RESULT vom Telemetry-Hintergrund-Task (alle 100 ms gepollt).
            // Lokales max tracken — wenn am Ende sgMax == 0 ist, hat sich der
            // Motor in keiner Halbschwingung erkennbar bewegt.
            uint16_t sg = Telemetry::getLatestSg(i);
            if (sg > sgMax) sgMax = sg;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (s->isRunning()) { s->stopMove(); Motion::waitWhileRunning(i, nullptr, 100); }
        dir = -dir;
    }
    outPulses = (int)(HalTacho::getPulseCount(i) - p0);
    outSgMax  = sgMax;
}

// Re-Sync zur physischen Sensor-Kante zwischen FS-Bändern.
//
// Hintergrund: Bei oszillierenden Tests mit Hysterese-Floor-Bändern (amp <
// Sensor-Hysterese) verliert der Motor pro Schwingung kumulativ Steps in
// eine Bevorzugungsrichtung (asymmetrische Beschleunigung + Stall-Verluste),
// auch wenn der Step-Counter logisch korrekt zählt. Über mehrere Bänder läuft
// der Motor mechanisch aus der Sensor-Zone raus (User-Befund 2026-05-09:
// „läuft im sweep nach links raus"). Der Step-Counter glaubt edgePos, physisch
// ist der Motor woanders → folgende Bänder schwingen um eine falsche Position.
//
// Lösung: Nach jedem Band Step-Counter physisch synchronisieren via Sensor.
// EdgeTouch zuerst (leicht, ~1 s), Homing::run als Fallback bei Miss (robust
// dank Bug-28-Fix, 3 rev Coverage). Setzt edgePos auf die physische Kante,
// canonicalisiert dir=+1.
//
// Returnt false nur bei Stop-Anforderung oder Total-Fehler — Caller bricht ab.
static bool fsResyncToEdge(uint8_t i, float edgeDeg, long& edgePos, int& dir) {
    // EdgeTouch: anfahren in CW-Richtung an die Sensor-EINTRITTSKANTE (LOW
    // = HIGH→LOW-Übergang, entspricht cal.triggerStartDeg). FS2/TCO schwingen
    // genau um diese Kante. backOff (0.15 rev = 54°) zuerst CCW raus aus
    // Sensor (falls drin), dann CW rein → erstes LOW = edgePos. 1 Sample,
    // weil mehr Samples den Drift erneut akkumulieren würden.
    long e = EdgeTouch::touch(i, LOW, +1, 3000, 1);
    if (e != LONG_MIN) {
        edgePos = e;
        dir = +1;
        return true;
    }
    // EdgeTouch miss → Motor weit gedriftet. Fallback auf Homing (CW+CCW
    // je 1.5 rev seit v4.1.8, deckt 3 rev ab).
    Logger::addLog("FS reSync: EdgeTouch miss → Homing fallback");
    if (Op::pendingStop) return false;
    Homing::run(i);
    auto* s = Stepper::get(i);
    if (!s) return false;
    Stepper::setMicrosteps(i, 64);
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, edgeDeg);
    if (!Motion::waitWhileRunning(i, &Op::pendingStop, 5000)) return false;
    edgePos = s->getCurrentPosition();
    dir = +1;
    return true;
}

// Tacho-Bisektion: sucht die größte amp im Bereich [FS2_AMP_MIN, ampMax]
// bei der `pulses >= swings/2` bleibt (= Sensor crossed regelmäßig). Wenn
// das oberste amp keine Pulse erzeugt → Hysterese hat zugeschlagen (= Sensor
// kann amp nicht mehr auflösen), wir loggen das als „Hysterese-Floor" und
// returnen 0. Sonst Bisektion bis Stabilität.
//
// SG_RESULT wird zur Info mit-geloggt, aber nicht zur Klassifizierung
// genutzt — auf dieser Hardware (Befund 2026-05-06) liefert SG bei
// oszillierender Bewegung konstant 0.
static long fs2BisectStallTacho(uint8_t i, float f, long ampMax,
                                 long edgePos, int& dir, float edgeDeg,
                                 long& edgePosOut, bool& outHysteresis) {
    outHysteresis = false;
    long lo = FS2_AMP_MIN;
    long hi = ampMax;
    edgePosOut = edgePos;
    if (hi <= lo) return lo;

    int swings, pulses; uint16_t sgMax;

    // Erst Top testen — wenn dort schon 0 Pulse, ist es entweder echter
    // Stall (Motor schafft amp×f² nicht) ODER Sensor-Hysterese (Motor
    // bewegt sich, kreuzt Kante aber nicht). Auf dieser Mechanik bei f≥14
    // ist es immer Hysterese. Wir loggen es und returnen 0.
    fs2TestPoint(i, f, hi, edgePos, dir, swings, pulses, sgMax);
    Logger::addLog(String("FS2 f=") + (int)f + " amp=" + hi
        + " p=" + pulses + "/" + swings + " sg=" + sgMax
        + (pulses == 0 ? " NO-PULSES" : ""));
    if (pulses == 0) {
        outHysteresis = true;
        return 0;  // Sensor sieht es nicht — Band für diese Hardware unbrauchbar
    }
    if (pulses >= swings / 2) return hi;  // Top läuft sauber, kein Stall

    // Hier: pulses zwischen 1 und swings/2-1 → Motor läuft, aber instabil.
    // Bisektion runter bis stabil.
    int iter = 0;
    long tol = max((long)2, ampMax / 50);
    while ((hi - lo) > tol && iter < 6 && !Op::pendingStop) {
        long mid = (lo + hi) / 2;
        fs2TestPoint(i, f, mid, edgePosOut, dir, swings, pulses, sgMax);
        Logger::addLog(String("FS2 f=") + (int)f + " amp=" + mid
            + " p=" + pulses + "/" + swings + " sg=" + sgMax);
        if (pulses >= swings / 2) {
            lo = mid;
        } else {
            hi = mid;
            // v4.4.15: Bug 54 (Re-Sync Deadlock). Re-Sync nur wenn amp groß
            // genug, dass der Motor die Sensor-Zunge physikalisch verlassen
            // kann (>30 steps auf Z). Bei winzigen Amps steht der Motor
            // bereits AUF der Kante, fsResyncToEdge meldet „miss" und triggert
            // einen fatalen Homing-Loop.
            if (mid > 30) {
                if (!fsResyncToEdge(i, edgeDeg, edgePosOut, dir)) break;
            } else {
                outHysteresis = true; // Klassifiziere als Sensor-Hysterese
            }
        }
        iter++;
    }
    return lo;
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
    Logger::addLog("FreqSweep v2 (Hysterese-Floor + Stall-Bisektion)");

    float edgeDeg = cal.triggerStartDeg;
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, edgeDeg);
    if (!waitOrStop(i, 5000)) { resetMotorState(i, false); return; }
    long edgePos = s->getCurrentPosition();
    int dir = 1;
    long sprQuarter = (long)Stepper::stepsPerRev(i) / 4;

    bool anyStall = false;
    int firstHysteresisBand = -1;
    for (uint8_t b = 0; b < FREQ_BANDS_V2 && !Op::pendingStop; b++) {
        float f = FREQ_BAND_HZ_V2[b];
        long ampPhysMax = (long)((float)FREQ_ACCEL_MAX / (16.0f * f * f));
        long ampMax = min(sprQuarter, max((long)FS2_AMP_MIN, ampPhysMax));

        bool hysteresis = false;
        long stallAmp = fs2BisectStallTacho(i, f, ampMax, edgePos, dir,
                                             edgeDeg, edgePos, hysteresis);
        cal.freqStallAmp[b] = (uint16_t)max((long)0, min((long)0xFFFF, stallAmp));
        if (hysteresis && firstHysteresisBand < 0) firstHysteresisBand = b;
        Logger::addLog(String("FS2 b=") + b + " f=" + (int)f
            + " stallAmp=" + cal.freqStallAmp[b]
            + (hysteresis ? " (hysteresis-floor)" : "")
            + " (ampMax=" + ampMax + ")");
        anyStall = true;
        Telemetry::recordDataPoint(i, "FS_BAND", f);

        // v4.3.3: Re-Sync zur physischen Sensor-Kante zwischen Bändern.
        // Step-Counter desynchronisiert sich pro Band durch Hysterese-Drift,
        // ohne Re-Sync läuft der Motor kumulativ aus der Sensor-Zone raus.
        if (b + 1 < FREQ_BANDS_V2 && !Op::pendingStop) {
            if (!fsResyncToEdge(i, edgeDeg, edgePos, dir)) break;
        }
    }
    if (firstHysteresisBand >= 0) {
        Logger::addLog(String("FS2 Sensor-Limit: erstmal Hysterese ab Band ")
            + firstHysteresisBand + " (f=" + (int)FREQ_BAND_HZ_V2[firstHysteresisBand]
            + " Hz)");
    }

    StorageCalib::save(i, cal);
    Logger::addLog("FreqSweep v2 fertig — freqStallAmp[] in NVS gespeichert");
    resetMotorState(i, anyStall);
}

// --- 6b) Tacho-Cutoff Diagnostik (FreqSweep v3 Phase A) ---
//
// Mißt die mechanische Hysterese-Grenze des induktiven Tachos: bei welcher
// Frequenz fallen die Pulse trotz physikalisch maximaler Amplitude auf 0.
// Spezifikation: v4/docs/spec_freqsweep_v3.md.
//
// Vorgehen pro Frequenz f∈[5..50 Hz, 1 Hz Step]:
//   1. amp = min(45° in Steps, ampPhysMax(f)) mit ampPhysMax = FREQ_ACCEL_MAX/(16·f²)
//      — das ist die größte Schwingweite, die der Motor bei f noch sauber
//      schafft (1/f²-Cap, identisch zum FS2-Modell). Konstantes 45° ist ab
//      f≈14 Hz physikalisch unmöglich (4·1037·14 = 58k sps + Beschleunigung).
//   2. posStart = getCurrentPosition()
//   3. fs2TestPoint schwingt um edgePos
//   4. posEnd = getCurrentPosition()
//   5. drift = posEnd - edgePos
//   6. log: f | amp | pulses | swings | drift | sg
//
// fc = höchste Frequenz mit pulses >= swings/2 (Tacho liefert noch).
// Drift wird mitgeschrieben, um „Hysterese-blind aber lebendig" (kleiner Drift)
// von „echter Total-Aussteiger" (großer Drift) zu unterscheiden — siehe
// AGENT_COORDINATION.md „Drift-Observation 10 Hz" und Bug 32.
//
// KEIN TMC-Eingriff: TCOOLTHRS bleibt 0, SG_RESULT-Spalte ist nur Mitschnitt
// (Bug 33 dokumentiert SG=0 bei Oszillation, nicht aussagekräftig in Phase A).
//
// Erwarteter Z-Motor-Befund (extrapoliert aus FS2-Hardware-Test 2026-05-09):
// fc≈10 Hz (bei f=10 noch p=3/5, bei f=12 schon p=0/6 in FS2). Phase A liefert
// dasselbe Ergebnis aber als eigenständiger Test mit feinem 1-Hz-Raster.
constexpr float    TCO_AMP_DEG_MAX = 45.0f;
constexpr uint8_t  TCO_F_START_HZ  = 5;
constexpr uint8_t  TCO_F_END_HZ    = 50;
constexpr uint8_t  TCO_NO_PULSE_RUNS = 3;  // 3× pulses==0 in Folge → Sweep stop

void runTachoCutoffDiagnostic(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("TCO: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("TCO: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 64);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog(String("TCO Start: ampMax=") + (int)TCO_AMP_DEG_MAX
        + " deg (1/f²-cap), " + TCO_F_START_HZ + ".." + TCO_F_END_HZ + " Hz");

    float edgeDeg = cal.triggerStartDeg;
    s->setSpeedInHz(8000); s->setAcceleration(20000);
    Motion::moveToDeg(i, edgeDeg);
    if (!waitOrStop(i, 5000)) { resetMotorState(i, false); return; }
    long edgePos = s->getCurrentPosition();
    int dir = 1;

    long ampDeg45Steps = Units::degToSteps(i, TCO_AMP_DEG_MAX);
    long sprQuarter = (long)Stepper::stepsPerRev(i) / 4;

    uint16_t fcutoff = 0;            // höchste f mit pulses >= swings/2
    uint8_t  noPulseStreak = 0;      // Anzahl aufeinanderfolgender pulses==0
    bool stalled = false;

    for (uint8_t f = TCO_F_START_HZ; f <= TCO_F_END_HZ && !Op::pendingStop; f++) {
        // Physikalisches amp-Maximum bei dieser Frequenz (1/f²-Modell aus FS2).
        long ampPhysMax = (long)((float)FREQ_ACCEL_MAX / (16.0f * (float)f * (float)f));
        long amp = min(ampDeg45Steps, min(sprQuarter, max((long)1, ampPhysMax)));

        long posStart = s->getCurrentPosition();
        int  swings = 0, pulses = 0; uint16_t sgMax = 0;
        fs2TestPoint(i, (float)f, amp, edgePos, dir,
                     swings, pulses, sgMax);
        long posEnd = s->getCurrentPosition();
        long drift  = posEnd - edgePos;

        Logger::addLog(String("[TCO] f=") + f
            + " amp=" + amp
            + " p=" + pulses + "/" + swings
            + " drift=" + drift
            + " sg=" + sgMax);
        Telemetry::recordDataPoint(i, "TCO", (float)f);

        if (pulses >= swings / 2 && swings > 0) {
            fcutoff = f;            // letzte „lebende" Frequenz
            noPulseStreak = 0;
        } else if (pulses == 0) {
            noPulseStreak++;
            if (noPulseStreak >= TCO_NO_PULSE_RUNS) {
                Logger::addLog(String("TCO: ") + TCO_NO_PULSE_RUNS
                    + "x p=0 in Folge → Sweep-Ende bei f=" + f);
                break;
            }
        }

        // Großer Drift = Motor läuft mechanisch weg (Step-Verlust oder
        // physischer Stall). Test sicherheitshalber abbrechen, Re-Homing.
        if (labs(drift) > sprQuarter) {
            Logger::addLog(String("TCO: Drift zu groß (")
                + drift + " > " + sprQuarter + ") → Stall, Abbruch");
            stalled = true;
            break;
        }

        // v4.3.3: Re-Sync zur physischen Sensor-Kante zwischen Frequenzen
        // (analog FS2). Step-Counter-basierter Vergleich (alter Code) reicht
        // nicht gegen kumulative Hysterese-Drift, weil der Counter den Drift
        // nicht sieht. Re-Sync nur wenn nicht letzte Frequenz.
        if (f < TCO_F_END_HZ && !Op::pendingStop) {
            if (!fsResyncToEdge(i, edgeDeg, edgePos, dir)) break;
        }
        (void)posStart;
    }

    cal.tachoCutoffHz = fcutoff;
    StorageCalib::save(i, cal);
    Logger::addLog(String("TCO fertig: fcutoff=") + fcutoff + " Hz "
        + "(in NVS gespeichert)");
    resetMotorState(i, stalled);
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
            uint16_t learned = mA + 100;
            Logger::addLog(String("Sweet-Spot Floor: ") + learned + "mA");
            cal.learnedCurrentMA = learned;
            StorageCalib::save(i, cal);
            stalled = true;
            break;
        }
    }
    resetMotorState(i, stalled);
}

// --- 7) ProfileLearn: Tacho-Perioden-Modell für Watchdog lernen ---
// Misst bei 10 RPM-Stützpunkten zwischen 300 und maxRpm die mittlere
// Tacho-Periode und deren Standardabweichung (Sigma). Watchdog nutzt dies
// zur Antizipation des nächsten Pulses (Evidence A).
void runProfileTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("Profile: nicht kalibriert"); return; }
    if (!HalPins::hasSensor(i)) { Logger::addLog("Profile: kein Sensor"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i);
    if (!s) { resetMotorState(i, false); return; }
    Logger::addLog(String("M") + v4::motorName(i) + ": Profile Learn");
    MotorProfileNs::clearProfile(i);

    float startRpm = 300.0f;
    float endRpm   = cal.maxRpm > 400 ? cal.maxRpm * 0.95f : 1000.0f;
    float stepRpm  = (endRpm - startRpm) / 9.0f;

    for (int k = 0; k < 10 && !Op::pendingStop; k++) {
        float rpm = startRpm + k * stepRpm;
        uint32_t targetSps = (uint32_t)Units::rpmToSps(i, rpm);
        s->setAcceleration(50000);
        s->setSpeedInHz(targetSps);
        s->runForward();
        delay(800); // Settle

        uint32_t p0 = HalTacho::getPulseCount(i);
        uint32_t samples[10]; int count = 0;
        unsigned long tStart = millis();
        while (count < 10 && millis() - tStart < 3000 && !Op::pendingStop) {
            uint32_t p1 = HalTacho::getPulseCount(i);
            if (p1 > p0) {
                samples[count++] = HalTacho::tacho[i].periodUs;
                p0 = p1;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (count >= 4) {
            double sum = 0;
            for (int n = 0; n < count; n++) sum += samples[n];
            double mean = sum / count;
            double var = 0;
            for (int n = 0; n < count; n++) var += pow((double)samples[n] - mean, 2);
            double sigma = sqrt(var / count);
            MotorProfileNs::addPoint(i, rpm, (float)mean, (float)sigma, Telemetry::getLatestSg(i), Telemetry::getLatestCs(i));
        }
    }
    MotorProfileNs::saveProfile(i);
    Logger::addLog("Profile: saved");
    resetMotorState(i, false);
}

// Performance Show: alle Tests in sinnvoller Reihenfolge.
// 1. SgLearn (Threshold lernen) → 2. SpeedTest (Top-RPM) → 3. InertiaTest
// (Top-Acc) → 4. ProfileLearn (Watchdog-Antizipation) → 5. Katapult (3 Bursts)
// → 6. CurrentSweep (Sweet-Spot) → 7. CoastTest (Auslauf) → 8. FreqSweep.
void runPerformanceShow(uint8_t i) {
    Logger::addLog("Vorführung Start");

    Logger::addLog("Show 1/8: SG-Learn");
    runSgLearn(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 2/8: SpeedTest");
    runSpeedTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 3/8: Inertia");
    runInertiaTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 4/8: ProfileLearn");
    runProfileTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 5/8: Katapult");
    runKatapult(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 6/8: CurrentSweep");
    runCurrentSweepHiRPM(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 7/8: Coast");
    runCoastTest(i);
    if (Op::pendingStop) return;

    Logger::addLog("Show 8/8: FreqSweep");
    runFreqSweep(i);

    Logger::addLog("Vorführung Ende");
}

} // namespace Characterization
