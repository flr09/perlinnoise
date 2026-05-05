#include "Homing.h"
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

namespace Homing {

// Schnellsuche in einer Drehrichtung. dir=+1 = CW (runForward), dir=-1 = CCW.
// Returns true wenn Sensor LOW innerhalb maxRev gefunden, sonst false (Motor
// gestoppt). Nach Bug-ID 28 (Hardware-Test 2026-05-05): nach Stall ist der
// Step-Counter desynchronisiert von der physischen Position — eine reine CW-
// Suche schlägt fehl, wenn die Zunge zufällig hinter dem aktuellen Stand liegt.
static bool searchSensorOneDir(uint8_t motorIdx, uint8_t pin, int dir, float maxRev) {
    auto* s = Stepper::get(motorIdx);
    if (!s) return false;
    s->setSpeedInHz(800);
    if (dir > 0) s->runForward(); else s->runBackward();
    long startPos = s->getCurrentPosition();
    long maxDelta = (long)((float)Stepper::stepsPerRev(motorIdx) * maxRev);
    unsigned long t0 = millis();
    bool found = false;
    while (true) {
        if (HalSensor::checkStable(pin, LOW, 5)) { found = true; break; }
        if (Op::pendingStop) break;
        if (millis() - t0 > 12000) break;
        if (labs(s->getCurrentPosition() - startPos) > maxDelta) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    s->stopMove();
    Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000);
    return found;
}

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

    // Bug-ID 28: zwei-Richtungen-Suche. Erst CW max 1.5 rev, bei Miss CCW
    // max 1.5 rev. Insgesamt 3 rev — deckt jeden Sensor-Sektor ab, auch wenn
    // der Step-Counter nach Stall vom physischen Stand abweicht.
    bool found = searchSensorOneDir(motorIdx, pin, +1, 1.5f);
    if (!found) {
        Logger::addLog("HOME: CW-Suche miss, versuche CCW...");
        found = searchSensorOneDir(motorIdx, pin, -1, 1.5f);
    }
    if (!found) { Logger::addLog("HOME: Sensor nicht gefunden (CW+CCW je 1.5 rev)"); return; }

    // 1-Touch-Bestätigung an der linken Kante
    Logger::addLog("HOME: Kante bestätigen...");
    long a1Now = EdgeTouch::touch(motorIdx, LOW, 1, 150, 1);
    if (a1Now == LONG_MIN) { Logger::addLog("HOME: Touch fehlgeschlagen"); return; }

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
