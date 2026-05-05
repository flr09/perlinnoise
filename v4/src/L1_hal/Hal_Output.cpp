#include "Hal_Output.h"
#include "Hal_Pins.h"

namespace HalOutput {

// LEDC-Channel 4 — 0..3 sind oft von anderen Libraries belegt (FastAccelStepper
// nutzt z.B. Hardware-Timer/PWM-Slots am unteren Ende).
static constexpr int FAN_CHANNEL = 4;
static constexpr int PWM_FREQ    = 5000;  // 5 kHz, oberhalb hörbarem Bereich
static constexpr int PWM_RES     = 8;     // 0..255

void init() {
    ledcSetup(FAN_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(HalPins::FAN_PIN, FAN_CHANNEL);
    ledcWrite(FAN_CHANNEL, 0);

    pinMode(HalPins::LAMP_PIN, OUTPUT);
    digitalWrite(HalPins::LAMP_PIN, LOW);
}

void setFan(uint8_t val) {
    ledcWrite(FAN_CHANNEL, val);
}

void setLamp(bool on) {
    digitalWrite(HalPins::LAMP_PIN, on ? HIGH : LOW);
}

} // namespace HalOutput
