#include "Sensor.h"
#include <FastAccelStepper.h>
#include <Bounce2.h>

// Defined in MotorControl.cpp — shared globals
extern portMUX_TYPE   motorMux;
extern FastAccelStepper* stepper;

// --- GLOBALS ---
Bounce sensorBounce;

volatile int16_t  lastSensorRaw  = 0;
volatile bool     sensorHit      = false;
long              pcntStepperBase = 0;

volatile unsigned long tachoPeriodMs  = 0;
volatile unsigned long lastTachoLowMs = 0;
volatile uint32_t      pulseCount     = 0;

// --- ISR ---
// Direct hardware register read — no function call, no Flash access, truly ISR-safe.
// pcnt_get_counter_value() is NOT IRAM_ATTR in ESP-IDF v4 → Flash fault when cache
// is disabled (WiFi init, OTA). PCNT peripheral registers are always accessible.
void IRAM_ATTR tachoISR() {
    // F12: Latch — nur erste Flanke nach sensorHit=false erfassen.
    // Bounce überschreibt lastSensorRaw nicht mehr.
    if (!sensorHit) {
        lastSensorRaw = (int16_t)(PCNT.cnt_unit[PCNT_UNIT_0].val & 0xFFFF);
        sensorHit = true;
    }
    if (digitalRead(TACHO_PIN) == LOW) {
        pulseCount++;
        // millis() is ARDUINO_ISR_ATTR in ESP32 Arduino — safe to call from ISR
        unsigned long now = millis();
        if (lastTachoLowMs > 0) {
            unsigned long p = now - lastTachoLowMs;
            if (p >= 5) tachoPeriodMs = p;   // ignore bounces / noise < 5 ms apart
        }
        lastTachoLowMs = now;
    }
}

uint16_t getTachoRpm() {
    unsigned long period, lastT;
    portENTER_CRITICAL(&motorMux);
    period = tachoPeriodMs;
    lastT  = lastTachoLowMs;
    portEXIT_CRITICAL(&motorMux);
    if (period == 0 || millis() - lastT > 2000) return 0;
    return (uint16_t)min(9999UL, 60000UL / period);
}

uint32_t getPulseCount() {
    uint32_t c;
    portENTER_CRITICAL(&motorMux);
    c = pulseCount;
    portEXIT_CRITICAL(&motorMux);
    return c;
}

bool waitForSensorTimed(bool state, long maxSteps, unsigned long timeoutMs) {
    if (!stepper) return false;
    unsigned long start    = millis();
    long          startPos = stepper->getCurrentPosition();
    while (true) {
        sensorBounce.update();
        if (sensorBounce.read() == state) return true;
        if (millis() - start > timeoutMs ||
            abs(stepper->getCurrentPosition() - startPos) > maxSteps) return false;
        yield();
    }
}

void initSensor() {
    // --- PCNT: configure BEFORE FastAccelStepper ---
    // The GPIO matrix has separate input and output paths.
    // PCNT uses the input path (GPIO → PCNT), RMT uses the output path (RMT → GPIO).
    // Order matters: pcnt_unit_config() calls gpio_output_disable() which calls
    // gpio_matrix_out(SIG_GPIO_OUT_IDX) — this would overwrite FAS's RMT routing if
    // called after. By configuring PCNT first, FAS's stepperConnectToPin() runs last
    // and sets up RMT routing correctly. PCNT input routing is untouched by FAS.
    pcnt_config_t pcnt_cfg = {};
    pcnt_cfg.pulse_gpio_num = X_STEP;
    pcnt_cfg.ctrl_gpio_num  = X_DIR;
    pcnt_cfg.pos_mode   = PCNT_COUNT_INC;     // rising edge of STEP → count
    pcnt_cfg.neg_mode   = PCNT_COUNT_DIS;     // falling edge → ignore
    pcnt_cfg.lctrl_mode = PCNT_MODE_REVERSE;  // DIR LOW (backward) → count down
    pcnt_cfg.hctrl_mode = PCNT_MODE_KEEP;     // DIR HIGH (forward) → count up
    pcnt_cfg.counter_h_lim = 32767;
    pcnt_cfg.counter_l_lim = -32768;
    pcnt_cfg.unit    = PCNT_UNIT_0;
    pcnt_cfg.channel = PCNT_CHANNEL_0;
    pcnt_unit_config(&pcnt_cfg);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);

    sensorBounce.attach(TACHO_PIN, INPUT_PULLUP);
    sensorBounce.interval(5);  // 5 ms — entspricht ISR-Noise-Filter
    attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoISR, CHANGE);
}
