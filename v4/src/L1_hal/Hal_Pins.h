#pragma once
#include <Arduino.h>

// FYSETC E4 — Pin-Map für 4 Motoren
//
// Quelle: v3/PROJEKT_DOKU.md (für X verifiziert) plus FYSETC E4-Standardbelegung.
// FSD Q1/Q2: Y/Z/E STEP+DIR und Y-MIN/Z-MIN-Pins sind noch zu verifizieren — die
// hier eingetragenen Werte sind die typische FYSETC-E4-Pinbelegung und müssen
// vor Phase-2-Tests am echten Board geprüft werden.

namespace HalPins {

constexpr int MOTOR_COUNT = 4;

enum MotorIndex : uint8_t { MOTOR_X = 0, MOTOR_Y = 1, MOTOR_Z = 2, MOTOR_E = 3 };

struct MotorPins {
    uint8_t step;
    uint8_t dir;
    uint8_t tachoPin;     // 0xFF = kein Sensor (nur für E)
    uint8_t uartAddress;  // TMC2209 UART-Slave-Adresse
};

// X verifiziert (v3). Y/Z/E STEP/DIR und Y-MIN/Z-MIN pins zu verifizieren (FSD Q1/Q2).
constexpr MotorPins MOTORS[MOTOR_COUNT] = {
    /* X */ { 27, 26, 15,   1 },
    /* Y */ { 33, 32, 34,   3 },
    /* Z */ { 14, 12, 39,   0 },
    /* E */ { 16, 17, 0xFF, 2 },  // E hat keinen Endstop
};

// Gemeinsame Pins
constexpr uint8_t ENABLE_PIN = 25;
constexpr uint8_t UART_RX    = 21;
constexpr uint8_t UART_TX    = 22;

constexpr bool hasSensor(uint8_t i) {
    return i < MOTOR_COUNT && MOTORS[i].tachoPin != 0xFF;
}

} // namespace HalPins
