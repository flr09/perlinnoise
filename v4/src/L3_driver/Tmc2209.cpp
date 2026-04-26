#include "Tmc2209.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"
#include "../L1_hal/Hal_Pins.h"

namespace Tmc {

static TMC2209Stepper* driverX = nullptr;
static bool initOk = false;
static bool poweredX = false;

void init() {
    pinMode(HalPins::ENABLE_PIN, OUTPUT);
    digitalWrite(HalPins::ENABLE_PIN, HIGH);  // disabled at boot

    Serial2.begin(115200, SERIAL_8N1, HalPins::UART_RX, HalPins::UART_TX);

    // Motor X (UART-Adresse 1)
    driverX = new TMC2209Stepper(&Serial2, R_SENSE, HalPins::MOTORS[HalPins::MOTOR_X].uartAddress);

    if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        driverX->begin();
        xSemaphoreGive(Sync::uartMutex);
        initOk = true;
        Logger::addLog("TMC: X init OK");
    } else {
        Logger::addLog("TMC: uartMutex timeout in init");
    }
}

bool ready() { return initOk; }

void applyDefaultsX(uint16_t runMA) {
    if (!initOk || !driverX) return;
    if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
    driverX->rms_current(runMA, hF);
    driverX->microsteps(64);
    driverX->iholddelay(10);
    driverX->pwm_autoscale(true);
    xSemaphoreGive(Sync::uartMutex);
}

void setPowerX(bool on) {
    if (!initOk || !driverX) return;
    if (on) {
        digitalWrite(HalPins::ENABLE_PIN, LOW);
        if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX->toff(5);
            xSemaphoreGive(Sync::uartMutex);
        }
        applyDefaultsX();
        Logger::addLog("X: POWER ON");
    } else {
        if (xSemaphoreTake(Sync::uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX->toff(0);
            xSemaphoreGive(Sync::uartMutex);
        }
        digitalWrite(HalPins::ENABLE_PIN, HIGH);
        Logger::addLog("X: POWER OFF");
    }
    poweredX = on;
}

bool isPoweredX() { return poweredX; }

} // namespace Tmc
