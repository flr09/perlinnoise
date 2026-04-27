#pragma once
#include <Arduino.h>

// L5b — Live-Parameter für die Bewegungs-Synthese.
// Werte aus V1 main.cpp übernommen + auf 4 Motoren mit individuellen Offsets.

namespace v4 {

struct RuntimeConfig {
    bool  running    = false;
    int   moveType   = 0;       // 0=Linear, 1=Circle, 2=Figure8, 3=Sinus, 4=Sawtooth, 5=Square, 6=STEP
    float speed      = 0.12f;   // Flugspeed bzw. Wellenfrequenz
    float angle      = 45.0f;   // Richtung bei Linear, in Grad
    float radius     = 50.0f;   // Pfadradius (Circle/Figure8)
    float framesize  = 0.01f;   // Noise-Zoom
    float contrast   = 1.0f;    // Amplitude
    float zShape     = 1.0f;    // Form-Exponent / Duty / Sawtooth-Form
    float edgeC      = 0.0f;    // Edge contrast (Noise)
    float rangeDeg   = 300.0f;  // Maximaler Hub in Grad
    float mspace     = 25.0f;   // Motor-Spacing (Noise) bzw. Phasen-Spread (Wellenform)
    int   dynamics   = 1;       // 0=Langsam, 1=Normal, 2=Rasant
    int   fan        = 0;       // PWM Fan 0..255
    int   lamp       = 0;       // PWM Lamp 0..255
    // --- STEP-Modus (moveType=6, Quader-Drehung) ---
    float stepAngle  = 90.0f;   // Sprungwinkel pro Schritt (4 Schritte ergeben Voll-Umdrehung bei 90°)
    float stepOffset = 0.0f;    // zusätzlicher Winkel-Offset, kumulativ pro Schritt
    float holdMs     = 40.0f;   // Verweildauer pro Position [ms] — bei 40ms Hold + schneller Bewegung ~25/s
    uint32_t accelMax = 100000; // max. Beschleunigung [sps²] — wird bis Cal-Max hochgezogen
};

extern RuntimeConfig rt;

} // namespace v4
