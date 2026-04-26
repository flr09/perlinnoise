#include "Homing.h"
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

namespace Homing {

void run(uint8_t motorIdx) {
    if (!HalPins::hasSensor(motorIdx)) {
        Logger::addLog(String("HOME M") + (char)('X' + motorIdx) + ": kein Sensor — bitte SetZero");
        return;
    }
    auto* s = Stepper::get(motorIdx);
    if (!s) return;
    uint8_t pin = HalPins::MOTORS[motorIdx].tachoPin;

    Tmc::setPower(motorIdx, true);
    Stepper::setMicrosteps(motorIdx, 16);
    Logger::addLog(String("HOME M") + (char)('X' + motorIdx) + ": v4 robust");
    s->setAcceleration(2000);

    // Schnellsuche CW
    s->setSpeedInHz(800);
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
    if (!found) { Logger::addLog("HOME: Sensor nicht gefunden"); return; }
    if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) return;

    // 1-Touch-Bestätigung an der linken Kante
    Logger::addLog("HOME: Kante bestätigen...");
    long a1Now = EdgeTouch::touch(motorIdx, LOW, 1, 150, 1);
    if (a1Now < 0) { Logger::addLog("HOME: Touch fehlgeschlagen"); return; }

    v4::CalibrationData cal;
    StorageCalib::load(motorIdx, cal);
    if (cal.valid) {
        s->setCurrentPosition(Units::degToSteps(motorIdx, cal.triggerStartDeg));
        Motion::moveToDeg(motorIdx, 0.0f);
        if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 10000)) return;
        Motion::setPositionDeg(motorIdx, 0.0f);
        Logger::addLog("HOME: @0° OK");
    } else {
        s->setCurrentPosition(0);
        Logger::addLog("HOME: fertig (uncal)");
    }
    Stepper::setMicrosteps(motorIdx, 64);
    Tmc::applyDefaults(motorIdx);
}

} // namespace Homing
