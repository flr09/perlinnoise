#include "SetZero.h"
#include "../L0_platform/Logger.h"
#include "../L3_driver/Motion.h"

namespace SetZero {

void apply(uint8_t motorIdx) {
    if (motorIdx >= 4) return;
    Motion::setPositionDeg(motorIdx, 0.0f);
    Logger::addLog(String("M") + (char)('X' + motorIdx) + ": SET ZERO");
}

} // namespace SetZero
