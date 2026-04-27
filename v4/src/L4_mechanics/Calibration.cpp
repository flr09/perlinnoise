#include "Calibration.h"
#include <limits.h>
#include "EdgeTouch.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Sensor.h"
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

void run(uint8_t motorIdx) {
    if (!HalPins::hasSensor(motorIdx)) {
        Logger::addLog(String("CAL M") + (char)('X' + motorIdx) + ": kein Sensor — abgelehnt");
        return;
    }
    auto* s = Stepper::get(motorIdx);
    if (!s) return;

    uint8_t pin = HalPins::MOTORS[motorIdx].tachoPin;
    Tmc::setPower(motorIdx, true);
    Stepper::setMicrosteps(motorIdx, 16);
    Logger::addLog(String("CAL M") + (char)('X' + motorIdx) + ": v4 calib (fast)");
    s->setAcceleration(15000);
    CAL_MARK("CAL_START", 0);

    // Vorbereitung: Sensor verlassen (falls aktiv)
    while (digitalRead(pin) == LOW) {
        if (Op::pendingStop) return;
        s->setSpeedInHz(2500);
        s->runBackward();
        unsigned long t0 = millis();
        bool exited = false;
        while (millis() - t0 < 3000) {
            if (HalSensor::checkStable(pin, HIGH, 5)) { exited = true; break; }
            if (Op::pendingStop) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!exited) break;
    }
    s->stopMove();
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 1500)) { CAL_MARK("CAL_P0_TIMEOUT", 0); return; }
    delay(80);
    CAL_MARK("CAL_P0_OK", s->getCurrentPosition());

    // Phase 1+2: Grob CW Suche Eintritt (LOW) und dann nahtlos weiter zum
    // Austritt (HIGH). Zwischen den Detection-Punkten KEIN stopMove —
    // sonst Decel-Overshoot (~46° bei 3500 sps + 15k Acc), Motor landet
    // außerhalb der Zunge bevor Phase 2 startet → false-positive HIGH.
    // Eine durchgehende CW-Bewegung umgeht das.
    Logger::addLog("CAL: P1+P2 Grob CW (Eintritt → Austritt)...");
    s->setSpeedInHz(2000);   // moderater, kürzere Decel falls doch nötig
    s->runForward();
    long startPos = s->getCurrentPosition();
    long maxDelta = (long)Stepper::stepsPerRev(motorIdx) * 3;  // 3 rev: P1 + Zunge
    unsigned long t0 = millis();
    bool found = false;
    while (true) {
        if (HalSensor::checkStable(pin, LOW, 5)) { found = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 15000) break;
        if (labs(s->getCurrentPosition() - startPos) > maxDelta) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (!found) {
        s->stopMove();
        Logger::addLog("ERR: P1 Eintritt");
        CAL_MARK("CAL_P1_FAIL", s->getCurrentPosition());
        return;
    }
    CAL_MARK("CAL_P1_FOUND", s->getCurrentPosition());

    // Motor läuft weiter CW — jetzt Austritt suchen (HIGH).
    long startPos2 = s->getCurrentPosition();
    bool exited = false;
    while (true) {
        if (HalSensor::checkStable(pin, HIGH, 5)) { exited = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 18000) break;
        if (labs(s->getCurrentPosition() - startPos2) > (long)Stepper::stepsPerRev(motorIdx)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    long zungenBreite = labs(s->getCurrentPosition() - startPos2);
    if (!exited) {
        Logger::addLog(String("ERR: P2 Austritt nach ") + zungenBreite + " steps");
        CAL_MARK("CAL_P2_FAIL", zungenBreite);
        return;
    }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) { CAL_MARK("CAL_P2_TIMEOUT", 0); return; }
    delay(80);
    // Position bei P2_OK = Zungen-Austritt (CW von rechts). Zungen-Eintritt
    // ist in P1_FOUND geloggt. Zungenbreite = pos(P2_OK) - pos(P1_FOUND).
    CAL_MARK("CAL_P2_OK", s->getCurrentPosition());

    // Phase 3: rechte Kante A2 (3-Touch CCW)
    Logger::addLog("CAL: P3 rechte Kante (3-Touch)...");
    long a2 = EdgeTouch::touch(motorIdx, LOW, -1, 800, 3);
    if (a2 == LONG_MIN) { Logger::addLog("ERR: P3 Touch"); CAL_MARK("CAL_P3_FAIL", 0); return; }
    CAL_MARK("CAL_A2", a2);

    // Phase 4: EdgeTouch macht intern eigenen Backoff — kein expliziter
    // Anlauf nötig.
    Logger::addLog("CAL: P4 linke Kante (3-Touch)...");
    long a1 = EdgeTouch::touch(motorIdx, LOW, 1, 800, 3);
    if (a1 == LONG_MIN) { Logger::addLog("ERR: P4 Touch"); CAL_MARK("CAL_P4_FAIL", 0); return; }
    CAL_MARK("CAL_A1", a1);

    // Berechnung & Speicherung
    long center = (a1 + a2) / 2;
    v4::CalibrationData cal;
    StorageCalib::load(motorIdx, cal);
    cal.triggerStartDeg = Units::stepsToDeg(motorIdx, a1 - center);
    cal.triggerEndDeg   = Units::stepsToDeg(motorIdx, a2 - center);
    cal.valid           = true;
    StorageCalib::save(motorIdx, cal);
    CAL_MARK("CAL_CENTER", center);

    // Auf Mitte fahren und Nullpunkt setzen
    Logger::addLog("CAL: Mitte → 0°");
    s->setSpeedInHz(1500);
    s->moveTo(center);
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 10000)) {
        CAL_MARK("CAL_MOVE_TIMEOUT", s->getCurrentPosition());
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
