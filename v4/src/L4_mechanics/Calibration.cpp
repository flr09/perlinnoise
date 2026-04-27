#include "Calibration.h"
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
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 1500)) return;
    delay(80);

    // Phase 1: Grob CW Suche Eintritt
    Logger::addLog("CAL: P1 Grob CW...");
    s->setSpeedInHz(3500);
    s->runForward();
    long startPos = s->getCurrentPosition();
    long maxDelta = (long)Stepper::stepsPerRev(motorIdx) * 2;
    unsigned long t0 = millis();
    bool found = false;
    while (true) {
        if (HalSensor::checkStable(pin, LOW, 5)) { found = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 15000) break;
        if (labs(s->getCurrentPosition() - startPos) > maxDelta) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    if (!found) { Logger::addLog("ERR: P1 Eintritt"); return; }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) return;

    // Phase 2: Austritt finden — bis zu 2 volle Umdrehungen, damit auch breite Zungen passen
    Logger::addLog("CAL: P2 Austritt...");
    s->setSpeedInHz(2000);
    s->runForward();
    startPos = s->getCurrentPosition();
    long maxDelta2 = (long)Stepper::stepsPerRev(motorIdx) * 2;
    t0 = millis();
    bool exited = false;
    while (true) {
        if (HalSensor::checkStable(pin, HIGH, 5)) { exited = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 15000) break;
        if (labs(s->getCurrentPosition() - startPos) > maxDelta2) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    long delta = labs(s->getCurrentPosition() - startPos);
    if (!exited) {
        Logger::addLog(String("ERR: P2 Austritt nach ") + delta + " steps (= " +
                       String((float)delta * 360.0f / Stepper::stepsPerRev(motorIdx), 0) + "°)");
        return;
    }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) return;
    delay(200);

    // Phase 3: rechte Kante A2 (3-Touch CCW)
    Logger::addLog("CAL: P3 rechte Kante (3-Touch)...");
    long a2 = EdgeTouch::touch(motorIdx, LOW, -1, 800, 3);
    if (a2 < 0) { Logger::addLog("ERR: P3 Touch"); return; }

    // Phase 4: linke Kante A1 (3-Touch CW). EdgeTouch macht intern eigenen
    // Backoff — daher kein expliziter Anlauf mehr nötig (war redundant).
    Logger::addLog("CAL: P4 linke Kante (3-Touch)...");
    s->setSpeedInHz(2000);
    s->runBackward();
    long startPos4 = s->getCurrentPosition();
    long maxDelta4 = (long)Stepper::stepsPerRev(motorIdx);
    t0 = millis();
    bool back = false;
    while (true) {
        if (HalSensor::checkStable(pin, HIGH, 5)) { back = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 10000) break;
        if (labs(s->getCurrentPosition() - startPos4) > maxDelta4) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    if (!back) { Logger::addLog("ERR: P4 Anlauf"); return; }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) return;
    delay(200);

    long a1 = EdgeTouch::touch(motorIdx, LOW, 1, 800, 3);
    if (a1 < 0) { Logger::addLog("ERR: P4 Touch"); return; }

    // Berechnung & Speicherung
    long center = (a1 + a2) / 2;
    v4::CalibrationData cal;
    StorageCalib::load(motorIdx, cal);
    cal.triggerStartDeg = Units::stepsToDeg(motorIdx, a1 - center);
    cal.triggerEndDeg   = Units::stepsToDeg(motorIdx, a2 - center);
    cal.valid           = true;
    StorageCalib::save(motorIdx, cal);

    // Auf Mitte fahren und Nullpunkt setzen
    Logger::addLog("CAL: Mitte → 0°");
    s->setSpeedInHz(1500);
    s->moveTo(center);
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 10000)) return;
    Motion::setPositionDeg(motorIdx, 0.0f);

    Stepper::setMicrosteps(motorIdx, 64);
    Tmc::applyDefaults(motorIdx);
    Logger::addLog("CAL: fertig");
}

} // namespace Calibration
