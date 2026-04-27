#include "EdgeTouch.h"
#include <limits.h>
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L1_hal/Hal_Sensor.h"
#include "../L3_driver/Stepper.h"
#include "../L3_driver/Motion.h"
#include "../L6_telemetry_safety/OpState.h"

namespace EdgeTouch {

long touch(uint8_t motorIdx, int targetSensorState, int dir,
           uint32_t speedSps, int samples) {
    auto* s = Stepper::get(motorIdx);
    if (!s || !HalPins::hasSensor(motorIdx)) return LONG_MIN;
    uint8_t sensorPin = HalPins::MOTORS[motorIdx].tachoPin;

    long sum = 0;
    for (int n = 0; n < samples; n++) {
        // Stück wegfahren — vom Sensor weg, in Gegenrichtung. Muss größer als
        // Zungenbreite sein (typisch ~30°), sonst false-positive Sensor-LOW
        // beim Re-Anfahrt. 0.15 rev = 54° bei 16MS — sicher außerhalb.
        s->setSpeedInHz(3000);
        long backOff = (long)((float)Stepper::stepsPerRev(motorIdx) * 0.15f);
        if (dir > 0) s->move(-backOff);
        else         s->move(backOff);
        if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 2000)) return LONG_MIN;
        delay(30);

        // Kante anfahren
        s->setSpeedInHz(speedSps);
        if (dir > 0) s->runForward();
        else         s->runBackward();

        long startPos = s->getCurrentPosition();
        // maxDelta muss > backOff(0.15) + Zungenbreite + Margin sein, sonst
        // bricht Anfahrt ab BEVOR Sensor erreicht. 0.4 rev = 144° → reicht
        // für Zungen bis ~80° + 54° backOff + Sicherheit.
        long maxDelta = (long)((float)Stepper::stepsPerRev(motorIdx) * 0.4f);
        unsigned long start = millis();
        bool hit = false;
        while (true) {
            if (HalSensor::checkStable(sensorPin, targetSensorState, 5)) { hit = true; break; }
            if (Op::pendingStop) break;
            if (millis() - start > 3000) break;
            if (labs(s->getCurrentPosition() - startPos) > maxDelta) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!hit) {
            s->stopMove();
            Logger::addLog(String("EdgeTouch M") + (char)('X' + motorIdx) + ": miss");
            return LONG_MIN;  // Sentinel für Miss — Position kann legitim negativ sein!
        }
        long pos = s->getCurrentPosition();
        sum += pos;
        s->stopMove();
        if (!Motion::waitWhileRunning(motorIdx, &Op::pendingStop, 1500)) return LONG_MIN;
        delay(30);
    }
    return sum / samples;
}

} // namespace EdgeTouch
