#pragma once
#include <Arduino.h>

// FYSETC E4 — Pin-Map für 4 Motoren
//
// Quelle: offizielles FYSETC-E4 GitHub-README (verifiziert 2026-04-26).
// https://github.com/FYSETC/FYSETC-E4
//
// **Achtung Hardware-Hinweis aus v3-Erfahrung:**
// In v3 wurde der Sensor am Z-MIN-Pin (GPIO 15) mit dem X-Motor-Treiber (UART
// Adresse 1, Step/Dir 27/26) verwendet — eine v3-spezifische Verkabelung.
// Diese Pin-Map dokumentiert die *offizielle* FYSETC-Belegung. Welcher Motor
// physisch aktiv ist, regelt die Phase-2-Test-Konfiguration, nicht diese Datei.
//
// **GPIO-Eigenheiten:**
// - GPIO 34/35 sind input-only ohne internen Pull-up → externer Pull-up auf
//   3,3 V nötig für NPN-NO-Sensoren (LJ12A3-4-Z/BX) an X-MIN/Y-MIN.
// - GPIO 15 ist Strapping-Pin und Bidirektional → interner Pull-up möglich
//   (`INPUT_PULLUP`), so wie es in v3 für TACHO genutzt wurde.

namespace HalPins {

constexpr int MOTOR_COUNT = 4;

enum MotorIndex : uint8_t { MOTOR_X = 0, MOTOR_Y = 1, MOTOR_Z = 2, MOTOR_E = 3 };

struct MotorPins {
    uint8_t step;
    uint8_t dir;
    uint8_t tachoPin;     // 0xFF = kein Sensor (nur für E)
    uint8_t uartAddress;  // TMC2209 UART-Slave-Adresse
    bool    tachoNeedsExtPullup;  // true → GPIO ist input-only, externer Pull-up nötig
};

// FYSETC-E4 offizielle Belegung (verifiziert 2026-04-26 aus dem GitHub-README).
constexpr MotorPins MOTORS[MOTOR_COUNT] = {
    /* X */ { 27, 26, 34,   1, true  },  // X-MIN = GPIO 34 (input-only)
    /* Y */ { 33, 32, 35,   3, true  },  // Y-MIN = GPIO 35 (input-only)
    /* Z */ { 14, 12, 15,   0, false },  // Z-MIN = GPIO 15 (interner Pull-up möglich)
    /* E */ { 16, 17, 0xFF, 2, false },  // E hat keinen Endstop-Pin
};

// Gemeinsame Pins
constexpr uint8_t ENABLE_PIN = 25;
constexpr uint8_t UART_RX    = 21;
constexpr uint8_t UART_TX    = 22;

// MOSFET-Ausgänge am FYSETC E4
constexpr uint8_t FAN_PIN    = 13;  // BED-MOSFET → Lüfter (PWM)
constexpr uint8_t LAMP_PIN   = 2;   // HOTEND-MOSFET → Lampe (discrete on/off; externer Treiber dimmt)

constexpr bool hasSensor(uint8_t i) {
    return i < MOTOR_COUNT && MOTORS[i].tachoPin != 0xFF;
}

} // namespace HalPins
