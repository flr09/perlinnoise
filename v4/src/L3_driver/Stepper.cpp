#include "Stepper.h"
#include "Tmc2209.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"
#include "../L0_platform/Types.h"

namespace Stepper {

static FastAccelStepperEngine engine = FastAccelStepperEngine();
static FastAccelStepper* steppers[4] = { nullptr, nullptr, nullptr, nullptr };
static uint16_t currentMs[4]  = { 64, 64, 64, 64 };
static uint16_t stepsPerRevs[4] = { 12800, 12800, 12800, 12800 };

void init() {
    engine.init();
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        steppers[i] = engine.stepperConnectToPin(HalPins::MOTORS[i].step);
        if (steppers[i]) {
            steppers[i]->setDirectionPin(HalPins::MOTORS[i].dir);
            steppers[i]->setEnablePin(HalPins::ENABLE_PIN, true);
            steppers[i]->setAutoEnable(false);
        } else {
            Logger::addLog(String("FAS: M") + v4::motorName(i) + " connect failed");
        }
    }
    Logger::addLog("FAS: 4x stepper engines ready");
}

FastAccelStepper* get(uint8_t motorIdx) {
    if (motorIdx >= 4) return nullptr;
    return steppers[motorIdx];
}

uint16_t microsteps(uint8_t motorIdx) {
    return motorIdx < 4 ? currentMs[motorIdx] : 0;
}

uint16_t stepsPerRev(uint8_t motorIdx) {
    return motorIdx < 4 ? stepsPerRevs[motorIdx] : 0;
}

void setMicrosteps(uint8_t motorIdx, uint16_t ms) {
    if (motorIdx >= 4) return;
    auto* s = steppers[motorIdx];
    if (!s) return;
    if (ms == currentMs[motorIdx] || s->isRunning()) return;

    portENTER_CRITICAL(&Sync::motorMux);
    long oldPos = s->getCurrentPosition();
    float factor = (float)ms / (float)currentMs[motorIdx];
    s->setCurrentPosition((long)lroundf((float)oldPos * factor));
    currentMs[motorIdx]    = ms;
    stepsPerRevs[motorIdx] = 200 * ms;
    portEXIT_CRITICAL(&Sync::motorMux);

    auto* d = Tmc::driver(motorIdx);
    if (d && xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        d->microsteps(ms);
        xSemaphoreGive(Sync::uartMutex);
    }
}

} // namespace Stepper
