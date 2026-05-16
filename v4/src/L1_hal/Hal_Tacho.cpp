#include "Hal_Tacho.h"
#include "Hal_Pins.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"

namespace HalTacho {

TachoState tacho[4];

// Software-Tacho-Polling per FreeRTOS-Task. attachInterrupt() ist auf GPIO 15
// nicht verwendbar (Strapping-Pin-Eigenheit → `gpio_isr_handler` Error), und
// auch auf den anderen Tacho-Pins wird konsequent gepollt, damit alle 4 Motoren
// dieselbe Detection-Mechanik haben. 1 kHz Polling: Nyquist >12 000 RPM bei 1
// PPR — deckt alle realistischen Tests ab. CPU-Last vernachlässigbar (~0.05 %).
// Tote ISR-Templates (FALLING-Trigger) wurden in v4.1.5 entfernt.
static int lastSensorState[4] = { -1, -1, -1, -1 };

static void tachoPollTask(void*) {
    for (;;) {
        for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
            if (!HalPins::hasSensor(i)) continue;
            int now = digitalRead(HalPins::MOTORS[i].tachoPin);
            if (lastSensorState[i] == -1) { lastSensorState[i] = now; continue; }
            if (lastSensorState[i] == HIGH && now == LOW) {
                tacho[i].pulseCount++;
                unsigned long us = micros();
                if (tacho[i].lastLowUs > 0) {
                    unsigned long p = us - tacho[i].lastLowUs;
                    if (p >= NOISE_FILTER_US) tacho[i].periodUs = p;
                }
                tacho[i].lastLowUs = us;
                tacho[i].latch = true;
            }
            lastSensorState[i] = now;
        }
        vTaskDelay(pdMS_TO_TICKS(1));  // 1kHz Polling
    }
}

void reattach() {
    for (uint8_t i = 0; i < HalPins::MOTOR_COUNT; i++) {
        if (!HalPins::hasSensor(i)) continue;
        uint8_t pin = HalPins::MOTORS[i].tachoPin;
        if (HalPins::MOTORS[i].tachoNeedsExtPullup) {
            pinMode(pin, INPUT);
        } else {
            pinMode(pin, INPUT_PULLUP);
        }
        Logger::addLog(String("TACHO M") + v4::motorName(i) + ": pin " + pin + " (poll-1kHz)");
    }
}

void init() {
    reattach();
    xTaskCreatePinnedToCore(tachoPollTask, "TachoPoll", 2048, nullptr, 2, nullptr, 1);
}

uint16_t getRpm(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    unsigned long periodUs, lastTUs;
    portENTER_CRITICAL(&Sync::motorMux);
    periodUs = tacho[motorIdx].periodUs;
    lastTUs  = tacho[motorIdx].lastLowUs;
    portEXIT_CRITICAL(&Sync::motorMux);
    if (periodUs == 0 || (micros() - lastTUs > STALE_TIMEOUT_MS * 1000UL)) return 0;
    return (uint16_t)min(9999UL, 60000000UL / periodUs);
}

uint32_t getPulseCount(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    uint32_t c;
    portENTER_CRITICAL(&Sync::motorMux);
    c = tacho[motorIdx].pulseCount;
    portEXIT_CRITICAL(&Sync::motorMux);
    return c;
}

void resetLatch(uint8_t motorIdx) {
    if (motorIdx >= 4) return;
    portENTER_CRITICAL(&Sync::motorMux);
    tacho[motorIdx].latch = false;
    portEXIT_CRITICAL(&Sync::motorMux);
}

bool latchFired(uint8_t motorIdx) {
    if (motorIdx >= 4) return false;
    return tacho[motorIdx].latch;
}

long latchPosition(uint8_t motorIdx) {
    if (motorIdx >= 4) return 0;
    return tacho[motorIdx].latchPos;
}

} // namespace HalTacho
