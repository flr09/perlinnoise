#include "SetZero.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"
#include "../L3_driver/Motion.h"

namespace SetZero {

void apply(uint8_t motorIdx) {
    if (motorIdx >= 4) return;
    Motion::setPositionDeg(motorIdx, 0.0f);
    Logger::addLog(String("M") + v4::motorName(motorIdx) + ": SET ZERO");
}

} // namespace SetZero
