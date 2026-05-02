#include "Hal_Tacho.h"
#include "Hal_Pins.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Logger.h"

namespace HalTacho {

TachoState tacho[4];

// FALLING-Trigger: ISR feuert nur bei HIGH→LOW. Spart das digitalRead in der ISR
// und vermeidet das Risiko dass die ISR auf der falschen Flanke kommt und
// digitalRead bereits umgeschaltet hat.

template<uint8_t I>
static void IRAM_ATTR tachoIsr() {
    tacho[I].pulseCount++;
    unsigned long now = micros();
    if (tacho[I].lastLowUs > 0) {
        unsigned long p = now - tacho[I].lastLowUs;
        if (p >= NOISE_FILTER_US) tacho[I].periodUs = p;
    }
    tacho[I].lastLowUs = now;
    tacho[I].latch = true;
}

// Hochfrequenter Software-Tacho-Poll-Task (5ms = 200 Hz). Polling im
// 50ms-Watchdog-Tick verfehlt bei hoher Acc Sensor-Durchquerungen
// (~30ms im Sensorfeld). Bei 5ms Polling: Nyquist bis ~12.000 RPM,
// deckt alle realistischen Tests ab.
//
// Kein attachInterrupt — der wirft auf GPIO 15 `gpio_isr_handler` Error
// (Strapping-Pin-Eigenheit). Polling per Task ist robust und CPU-Last
// vernachlässigbar (~0.05%).
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
        Logger::addLog(String("TACHO M") + (char)('X'+i) + ": pin " + pin + " (poll-1kHz)");
    }
    (void)tachoIsr<0>;
    (void)tachoIsr<1>;
    (void)tachoIsr<2>;
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
