#include "Calibration.h"
#include <limits.h>
#include "EdgeTouch.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Sensor.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L2_storage/Storage_Calib.h"
#include "../L3_driver/Stepper.h"
#include "../L3_driver/Tmc2209.h"
#include "../L3_driver/Motion.h"
#include "../L3_driver/Units.h"
#include "../L6_telemetry_safety/OpState.h"
#include "../L6_telemetry_safety/Telemetry.h"

// Marker via recordDataPoint (50ms-Throttle). Cal-Phasen haben durch
// Bewegung+Settle natürlich >>50ms Abstand — keine Marker gehen verloren.
// KEIN zusätzliches delay (das hatte Motor während Markierung weiterlaufen
// lassen → Position-Werte verfälscht).
#define CAL_MARK(phase, val) Telemetry::recordDataPoint(motorIdx, phase, (float)(val))

namespace Calibration {

// Toleranz (relativ) für 1-Touch-Skip via Zungenbreiten-Vergleich.
// ±5% Differenz zwischen gemessener (P1+P2 schnell) und gespeicherter Breite
// werden als „Mechanik unverändert" akzeptiert → P3+P4 (3-Touch) entfallen.
// Latenz-Bias (~5ms × 2000sps ≈ 10 Mikrosteps bei 16MS) ist auf beiden
// Kanten gleich → Zungenbreite ist invariant, nur die Mitte bekommt einen
// kleinen Offset (~1°), für Engineering-Tests akzeptabel.
constexpr float SKIP_TOL = 0.05f;

void run(uint8_t motorIdx) {
    if (!HalPins::hasSensor(motorIdx)) {
        Logger::addLog(String("CAL M") + v4::motorName(motorIdx) + ": kein Sensor — abgelehnt");
        return;
    }
    auto* s = Stepper::get(motorIdx);
    if (!s) return;

    // Bug 51/53 (v4.4.8): Cleanup-Helper für alle Abort-Pfade. Vorher ließ
    // jeder vorzeitige `return;` den Motor auf 16 µSteps + Calib-Stromprofil
    // hängen — Player oder Folge-Calib erbten das. Jetzt zentraler Restore.
    auto restoreDefaults = [motorIdx]() {
        Stepper::setMicrosteps(motorIdx, 64);
        Tmc::applyDefaults(motorIdx);
    };

    // Vorab: vorhandene NVS-Cal laden, um Skip-Pfad vorzubereiten.
    // Skip-Referenz ist `fastWidthSteps` aus dem letzten 3-Touch-Calib —
    // gemessen mit derselben P1+P2-Methodik wie der Skip-Check, daher
    // self-consistent (heben Sensor-Hysterese + Latenz-Bias gegeneinander auf).
    // 0 = noch unbekannt (frisch nach Schema-Bump) → kein Skip möglich.
    v4::CalibrationData prevCal;
    StorageCalib::load(motorIdx, prevCal);
    long expectedWidth = (prevCal.valid && prevCal.fastWidthSteps > 0)
        ? (long)prevCal.fastWidthSteps : 0;

    uint8_t pin = HalPins::MOTORS[motorIdx].tachoPin;
    Tmc::setPower(motorIdx, true);
    Stepper::setMicrosteps(motorIdx, 16);
    Logger::addLog(String("CAL M") + v4::motorName(motorIdx) + ": v4 calib (fast)"
        + (expectedWidth > 0 ? String(", fastW=") + expectedWidth
                             : String(", no fastW (3-Touch)")));
    s->setAcceleration(15000);
    CAL_MARK("CAL_START", 0);

    // Phase 0: Sensor verlassen falls aktiv. Hartes Distanz-Limit 1 rev —
    // mehr darf der Motor während Calib in keiner Phase fahren (Anti-Wickel-
    // Garantie + Geschwindigkeit). Wenn Sensor nach 1 rev CCW immer noch LOW
    // bleibt, ist mechanisch oder elektrisch was kaputt → klarer Abort.
    if (digitalRead(pin) == LOW) {
        s->setSpeedInHz(2500);
        s->runBackward();
        long startPos0 = s->getCurrentPosition();
        long maxP0 = (long)Stepper::stepsPerRev(motorIdx);  // 1 rev hart
        bool exited = false;
        while (true) {
            if (HalSensor::checkStable(pin, HIGH, 5)) { exited = true; break; }
            if (Op::pendingStop) { s->stopMove(); restoreDefaults(); return; }
            if (labs(s->getCurrentPosition() - startPos0) > maxP0) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        s->stopMove();
        if (!exited) {
            Logger::addLog(String("ERR: P0 Sensor stuck LOW nach ") + maxP0 + " steps (1 rev)");
            CAL_MARK("CAL_P0_STUCK", s->getCurrentPosition());
            restoreDefaults();
            return;
        }
    }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 1500)) { CAL_MARK("CAL_P0_TIMEOUT", 0); restoreDefaults(); return; }
    delay(80);
    CAL_MARK("CAL_P0_OK", s->getCurrentPosition());

    // Bug 51 (v4.4.13): Bidirektionale P1-Suche nach Bug-28-Präzedenz (Homing).
    // Vorher nur CW: nach einem Stall divergiert der Step-Counter vom
    // physischen Stand → Zunge kann „hinter" dem Motor liegen → CW läuft
    // 1.2 rev ins Leere, Calib failed mit „P1 Eintritt nicht gefunden". Jetzt
    // bei CW-Miss zusätzlich CCW-Suche — analog `Homing::searchSensorOneDir`.
    // Phase 2 läuft anschließend in derselben Richtung wie P1 erfolgreich war
    // (`dir = ±1`). Zungenbreite via `labs(p2-p1)` ist richtungsinvariant.
    Logger::addLog("CAL: P1+P2 Grob (Eintritt → Austritt)...");
    s->setSpeedInHz(2000);
    long maxDelta = (long)((float)Stepper::stepsPerRev(motorIdx) * 1.2f);
    long startPos = s->getCurrentPosition();
    int dir = 1;

    auto searchEntry = [&](int searchDir) -> bool {
        if (searchDir > 0) s->runForward(); else s->runBackward();
        long sp = s->getCurrentPosition();
        unsigned long ts = millis();
        unsigned long lastMoveCheck = millis();
        while (true) {
            if (HalSensor::checkStable(pin, LOW, 5)) return true;
            if (Op::pendingStop) return false;
            if (millis() - ts > 6000) return false;
            if (labs(s->getCurrentPosition() - sp) > maxDelta) return false;

            // Bug 51 (v4.4.15): Stall-Check via Tacho. Wenn Motor physisch
            // steht (RPM=0) aber laut FAS laufen sollte, liegt ein Stall vor.
            // Settle-Zeit 300ms für Anlauf beachten.
            if (millis() - ts > 300 && millis() - lastMoveCheck > 100) {
                if (HalTacho::getRpm(motorIdx) == 0) {
                    Logger::addLog(String("CAL M") + v4::motorName(motorIdx) + ": physischer Stall erkannt (RPM=0)");
                    return false;
                }
                lastMoveCheck = millis();
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
    };

    bool found = searchEntry(+1);
    if (!found) {
        s->stopMove();
        Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 1500);
        Logger::addLog("CAL: P1 CW miss → CCW retry (Bug-28-Coverage)");
        dir = -1;
        found = searchEntry(-1);
    }
    if (!found) {
        s->stopMove();
        long traveled = labs(s->getCurrentPosition() - startPos);
        Logger::addLog(String("ERR: P1 Eintritt nicht gefunden (CW+CCW je 1.2 rev, traveled=")
            + traveled + ")");
        CAL_MARK("CAL_P1_FAIL", traveled);
        restoreDefaults();
        return;
    }
    long p1Pos = s->getCurrentPosition();
    CAL_MARK("CAL_P1_FOUND", p1Pos);
    if (dir < 0) Logger::addLog(String("CAL: P1 via CCW gefunden bei pos=") + p1Pos);

    // Motor läuft weiter in `dir`-Richtung — jetzt Austritt (HIGH) suchen.
    // KRITISCH (Bug-Fix v4.1.3-rc2): p2Pos muss SOFORT beim Trigger erfasst
    // werden, vor stopMove. Sonst kommt der Bremsweg als systematische
    // Asymmetrie in die Zungenbreite — SKIP_TOL=5% würde nie greifen.
    // Logic-Check: p1Pos und p2Pos werden beide nach `checkStable(pin,…,5)`
    // erfasst, gleicher Latenz-Bias hebt sich bei der Differenz auf.
    long startPos2 = p1Pos;
    unsigned long tP2 = millis();
    bool exited = false;
    long p2Pos = 0;
    while (true) {
        if (HalSensor::checkStable(pin, HIGH, 5)) {
            p2Pos = s->getCurrentPosition();
            exited = true;
            break;
        }
        if (Op::pendingStop) break;
        if (millis() - tP2 > 10000) break;
        if (labs(s->getCurrentPosition() - startPos2) > (long)Stepper::stepsPerRev(motorIdx)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    if (!exited) {
        long zungenBreite = labs(s->getCurrentPosition() - startPos2);
        Logger::addLog(String("ERR: P2 Austritt nach ") + zungenBreite + " steps");
        CAL_MARK("CAL_P2_FAIL", zungenBreite);
        restoreDefaults();
        return;
    }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) { CAL_MARK("CAL_P2_TIMEOUT", 0); restoreDefaults(); return; }
    delay(80);
    CAL_MARK("CAL_P2_OK", p2Pos);

    // 1-Touch-Skip: gemessene Zungenbreite (P2-P1) gegen NVS vergleichen.
    // Bei Übereinstimmung sind die mechanischen Kanten unverändert → P3+P4
    // (3-Touch je 3s) entfallen, Mitte = (P1+P2)/2 ist gut genug.
    long center;
    if (prevCal.valid && expectedWidth > 0) {
        long measuredWidth = labs(p2Pos - p1Pos);
        long absDelta = labs(measuredWidth - expectedWidth);
        float relDelta = (float)absDelta / (float)expectedWidth;
        // Diagnose-Log: IMMER, damit auch im Miss-Fall sichtbar ist warum.
        Logger::addLog(String("CAL: check d=") + measuredWidth
            + " exp=" + expectedWidth
            + " Δ=" + absDelta + " (" + (int)(relDelta * 1000) + "‰)");
        if (relDelta <= SKIP_TOL) {
            center = (p1Pos + p2Pos) / 2;
            Logger::addLog(String("CAL: SKIP P3+P4 OK"));
            CAL_MARK("CAL_SKIP_OK", measuredWidth);
            CAL_MARK("CAL_CENTER", center);
            // triggerStartDeg/EndDeg bleiben aus NVS — Mechanik ist unverändert.
        } else {
            Logger::addLog(String("CAL: SKIP miss → 3-Touch fallback"));
            CAL_MARK("CAL_SKIP_FAIL", measuredWidth);
            // Fallback auf vollen 3-Touch-Pfad.
            Logger::addLog("CAL: P3 rechte Kante (3-Touch)...");
            long a2 = EdgeTouch::touch(motorIdx, LOW, -1, 800, 3);
            if (a2 == LONG_MIN) { Logger::addLog("ERR: P3 Touch"); CAL_MARK("CAL_P3_FAIL", 0); restoreDefaults(); return; }
            CAL_MARK("CAL_A2", a2);
            Logger::addLog("CAL: P4 linke Kante (3-Touch)...");
            long a1 = EdgeTouch::touch(motorIdx, LOW, 1, 800, 3);
            if (a1 == LONG_MIN) { Logger::addLog("ERR: P4 Touch"); CAL_MARK("CAL_P4_FAIL", 0); restoreDefaults(); return; }
            CAL_MARK("CAL_A1", a1);
            center = (a1 + a2) / 2;
            v4::CalibrationData cal = prevCal;
            cal.triggerStartDeg = Units::stepsToDeg(motorIdx, a1 - center);
            cal.triggerEndDeg   = Units::stepsToDeg(motorIdx, a2 - center);
            cal.valid           = true;
            // Self-Consistency: speichere die in DIESEM Run gemessene P1+P2-
            // Breite als neue Skip-Referenz. Beim nächsten Calib wird die
            // frische Messung gegen diesen Wert geprüft → Methodik konsistent.
            cal.fastWidthSteps  = (uint16_t)min((long)labs(p2Pos - p1Pos), (long)0xFFFF);
            StorageCalib::save(motorIdx, cal);
            Logger::addLog(String("CAL: fastWidth=") + cal.fastWidthSteps + " gespeichert");
            CAL_MARK("CAL_CENTER", center);
        }
    } else {
        // Erst-Calib (oder NVS leer/stale/Schema-Bump): voller 3-Touch.
        Logger::addLog("CAL: P3 rechte Kante (3-Touch)...");
        long a2 = EdgeTouch::touch(motorIdx, LOW, -1, 800, 3);
        if (a2 == LONG_MIN) { Logger::addLog("ERR: P3 Touch"); CAL_MARK("CAL_P3_FAIL", 0); restoreDefaults(); return; }
        CAL_MARK("CAL_A2", a2);
        Logger::addLog("CAL: P4 linke Kante (3-Touch)...");
        long a1 = EdgeTouch::touch(motorIdx, LOW, 1, 800, 3);
        if (a1 == LONG_MIN) { Logger::addLog("ERR: P4 Touch"); CAL_MARK("CAL_P4_FAIL", 0); restoreDefaults(); return; }
        CAL_MARK("CAL_A1", a1);
        center = (a1 + a2) / 2;
        // prevCal als Basis übernehmen (erhält learnedCurrentMA, sgThrs etc.,
        // selbst wenn Schema stale war — die Bytes wurden in load() gefüllt).
        v4::CalibrationData cal = prevCal;
        cal.triggerStartDeg = Units::stepsToDeg(motorIdx, a1 - center);
        cal.triggerEndDeg   = Units::stepsToDeg(motorIdx, a2 - center);
        cal.valid           = true;
        cal.fastWidthSteps  = (uint16_t)min((long)labs(p2Pos - p1Pos), (long)0xFFFF);
        StorageCalib::save(motorIdx, cal);
        Logger::addLog(String("CAL: fastWidth=") + cal.fastWidthSteps + " (initial)");
        CAL_MARK("CAL_CENTER", center);
    }

    // Auf Mitte fahren und Nullpunkt setzen
    Logger::addLog("CAL: Mitte → 0°");
    s->setSpeedInHz(1500);
    s->moveTo(center);
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 10000)) {
        CAL_MARK("CAL_MOVE_TIMEOUT", s->getCurrentPosition());
        restoreDefaults();
        return;
    }
    CAL_MARK("CAL_AT_CENTER", s->getCurrentPosition());
    Motion::setPositionDeg(motorIdx, 0.0f);
    CAL_MARK("CAL_ZERO_SET", s->getCurrentPosition());

    Stepper::setMicrosteps(motorIdx, 64);
    Tmc::applyDefaults(motorIdx);
    CAL_MARK("CAL_DONE", s->getCurrentPosition());
    Logger::addLog("CAL: fertig");
}

} // namespace Calibration
