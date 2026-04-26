#include "Tmc2209.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"

namespace Tmc {

static TMC2209Stepper* drivers[4] = { nullptr, nullptr, nullptr, nullptr };
static bool poweredFlags[4] = { false, false, false, false };
static bool initOk = false;

void init() {
    pinMode(HalPins::ENABLE_PIN, OUTPUT);
    digitalWrite(HalPins::ENABLE_PIN, HIGH);  // disabled at boot

    Serial2.begin(115200, SERIAL_8N1, HalPins::UART_RX, HalPins::UART_TX);

    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        drivers[i] = new TMC2209Stepper(&Serial2, R_SENSE, HalPins::MOTORS[i].uartAddress);
    }

    if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
            drivers[i]->begin();
        }
        xSemaphoreGive(Sync::uartMutex);
        initOk = true;
        Logger::addLog("TMC: 4x init OK");
    } else {
        Logger::addLog("TMC: uartMutex timeout in init");
    }
}

bool ready() { return initOk; }

TMC2209Stepper* driver(uint8_t motorIdx) {
    if (!initOk || motorIdx >= 4) return nullptr;
    return drivers[motorIdx];
}

void applyDefaults(uint8_t motorIdx, uint16_t runMA) {
    auto* d = driver(motorIdx);
    if (!d) return;
    if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
    d->rms_current(runMA, hF);
    d->microsteps(64);
    d->iholddelay(10);
    d->pwm_autoscale(true);
    xSemaphoreGive(Sync::uartMutex);
}

void setPower(uint8_t motorIdx, bool on) {
    auto* d = driver(motorIdx);
    if (!d) return;
    if (on) {
        // ENABLE-Pin ist gemeinsam — sobald er LOW ist, sind alle Treiber freigegeben.
        // Ob ein einzelner Motor wirklich Strom zieht, hängt von toff ab.
        digitalWrite(HalPins::ENABLE_PIN, LOW);
        if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            d->toff(5);
            xSemaphoreGive(Sync::uartMutex);
        }
        applyDefaults(motorIdx);
        Logger::addLog(String("M") + (char)('X' + motorIdx) + ": POWER ON");
    } else {
        if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            d->toff(0);
            xSemaphoreGive(Sync::uartMutex);
        }
        // ENABLE-Pin nur wenn KEIN anderer Motor mehr powered ist
        bool anyOn = false;
        for (uint8_t i = 0; i < 4; i++) if (i != motorIdx && poweredFlags[i]) { anyOn = true; break; }
        if (!anyOn) digitalWrite(HalPins::ENABLE_PIN, HIGH);
        Logger::addLog(String("M") + (char)('X' + motorIdx) + ": POWER OFF");
    }
    poweredFlags[motorIdx] = on;
}

bool isPowered(uint8_t motorIdx) {
    if (motorIdx >= 4) return false;
    return poweredFlags[motorIdx];
}

void setAllPower(bool on) {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) setPower(i, on);
}

// Backwards-compat
void applyDefaultsX(uint16_t runMA) { applyDefaults(0, runMA); }
void setPowerX(bool on)             { setPower(0, on); }
bool isPoweredX()                   { return isPowered(0); }

} // namespace Tmc
