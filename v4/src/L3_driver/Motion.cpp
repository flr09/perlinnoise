#include "Motion.h"
#include "Stepper.h"
#include "Units.h"

namespace Motion {

void moveToDeg(uint8_t motorIdx, float deg) {
    auto* s = Stepper::get(motorIdx);
    if (s) s->moveTo(Units::degToSteps(motorIdx, deg));
}

void moveByDeg(uint8_t motorIdx, float deg) {
    auto* s = Stepper::get(motorIdx);
    if (s) s->move(Units::degToSteps(motorIdx, deg));
}

void setPositionDeg(uint8_t motorIdx, float deg) {
    auto* s = Stepper::get(motorIdx);
    if (s) s->setCurrentPosition(Units::degToSteps(motorIdx, deg));
}

float getPositionDeg(uint8_t motorIdx) {
    auto* s = Stepper::get(motorIdx);
    return s ? Units::stepsToDeg(motorIdx, s->getCurrentPosition()) : 0.0f;
}

bool isRunning(uint8_t motorIdx) {
    auto* s = Stepper::get(motorIdx);
    return s ? s->isRunning() : false;
}

bool waitWhileRunning(uint8_t motorIdx, volatile bool* pendingStopFlag, unsigned long timeoutMs) {
    auto* s = Stepper::get(motorIdx);
    if (!s) return false;
    unsigned long start = millis();
    while (s->isRunning()) {
        if (pendingStopFlag && *pendingStopFlag) {
            s->stopMove();
            return false;
        }
        if (millis() - start > timeoutMs) {
            s->stopMove();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // gibt Scheduler frei (FSD: keine yield()-Klötze)
    }
    return true;
}

} // namespace Motion
