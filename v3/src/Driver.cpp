#include "Driver.h"
#include "Sensor.h"  // for pcntStepperBase reset after setMicrosteps (future use)

// Defined in MotorControl.cpp — shared globals
extern SystemState        sys;
extern portMUX_TYPE       motorMux;
extern SemaphoreHandle_t  uartMutex;
void addLog(String msg);  // defined in MotorControl.cpp

// --- GLOBALS ---
TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);

FastAccelStepperEngine engine  = FastAccelStepperEngine();
FastAccelStepper*      stepper = NULL;

uint16_t currentMicrosteps = 64;
uint16_t stepsPerRev       = 12800;  // 200 * 64

// --- UNIT CONVERSIONS ---
float    rpmToSps(float rpm)  { return (rpm * (float)stepsPerRev) / 60.0f; }
float    spsToRpm(float sps)  { return (sps * 60.0f) / (float)stepsPerRev; }
uint32_t rpmToTpwmthrs(float rpm) {
    float usps = rpm * (float)stepsPerRev / 60.0f;
    return (usps < 1.0f) ? 0xFFFFF : (uint32_t)(12000000.0f / usps);
}

// --- DRIVER SETTINGS ---
void applyDriverSettings(uint16_t runMA) {
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        float hF = min(0.5f, max(0.22f, 200.0f / (float)runMA));
        driverX.rms_current(runMA, hF);
        driverX.microsteps(currentMicrosteps);
        driverX.iholddelay(10);
        driverX.SGTHRS(sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT);
        driverX.TPWMTHRS(rpmToTpwmthrs(100));
        driverX.en_spreadCycle(false);
        driverX.TCOOLTHRS(rpmToTpwmthrs(50));
        driverX.pwm_autoscale(true);
        xSemaphoreGive(uartMutex);
    }
}

void setMicrosteps(uint16_t ms) {
    if (ms == currentMicrosteps || (stepper && stepper->isRunning())) return;
    portENTER_CRITICAL(&motorMux);
    long oldPos = stepper ? stepper->getCurrentPosition() : 0;
    float factor = (float)ms / (float)currentMicrosteps;
    if (stepper) stepper->setCurrentPosition((long)lroundf((float)oldPos * factor));
    currentMicrosteps = ms;
    stepsPerRev = 200 * ms;
    portEXIT_CRITICAL(&motorMux);
    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        driverX.microsteps(ms);
        xSemaphoreGive(uartMutex);
    }
}

void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        digitalWrite(ENABLE_PIN, LOW);
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.toff(5); xSemaphoreGive(uartMutex);
        }
        applyDriverSettings(sys.cal[0].learnedCurrentMA > 0
            ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
        addLog("M0 ON");
    } else {
        if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            driverX.toff(0); xSemaphoreGive(uartMutex);
        }
        digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

// --- ANGLE-ABSOLUTE API ---
void  moveToDeg(float deg)      { if (stepper) stepper->moveTo(degToSteps(deg)); }
void  moveByDeg(float deg)      { if (stepper) stepper->move(degToSteps(deg)); }
void  setPositionDeg(float deg) { if (stepper) stepper->setCurrentPosition(degToSteps(deg)); }
float getPositionDeg()          { return stepper ? stepsToDeg(stepper->getCurrentPosition()) : 0.0f; }

// --- INIT ---
void initDriver() {
    // FAS runs AFTER PCNT (initSensor must be called first).
    // stepperConnectToPin() routes RMT → X_STEP (output path).
    // PCNT input routing for X_STEP is unaffected.
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);

    engine.init();
    stepper = engine.stepperConnectToPin(X_STEP);
    if (stepper) {
        stepper->setDirectionPin(X_DIR);
        stepper->setEnablePin(ENABLE_PIN, true);
        stepper->setAutoEnable(false);
    }

    // F6 Fix: Enable Input-Buffer on Step/Dir pins AFTER FastAccelStepper init.
    // Uses low-level FUN_IE bit to avoid breaking RMT peripheral routing.
    enablePcntInputBuffer();

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        driverX.begin(); xSemaphoreGive(uartMutex);
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
}
