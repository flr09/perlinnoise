#include "OpState.h"

namespace Op {

volatile v4::OpState state       = v4::OpState::IDLE;
volatile bool        pendingStop = false;
PendingFlags         pending;

void requestPower(uint8_t motorIdx, bool on) {
    if (motorIdx >= 4) return;
    pending.powerOn[motorIdx] = on;
    pending.power = motorIdx;
}

void requestSetZero(uint8_t motorIdx) {
    if (motorIdx < 4) pending.setzero = motorIdx;
}

void requestHome(uint8_t motorIdx) {
    if (motorIdx < 4) pending.home = motorIdx;
}

void requestCalib(uint8_t motorIdx) {
    if (motorIdx < 4) pending.calib = motorIdx;
}

void requestStop() {
    pendingStop = true;
}

bool isBusy() {
    return state != v4::OpState::IDLE;
}

const char* stateStr() {
    return v4::opName(state);
}

} // namespace Op
