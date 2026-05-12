#pragma once
#include <Arduino.h>

// L0 — Daten-Typen, layer-übergreifend genutzt.
//
// Schichten-Regel: L2 (Storage) muss CalibrationData kennen, dürfte aber nicht
// auf L4 zugreifen — also liegen die reinen Daten-Typen hier in L0.

namespace v4 {

// --- Räumliche 2D-Position (Phase 10) ---
//
// Wird für `RuntimeConfig.offsets[4]` benutzt: pro Motor eine (x,y)-Position
// im logischen Noise-Raum. Synthesis sampelt den Noise an
// `(flightX+offset.x, flightY+offset.y)` statt entlang einer geraden Linie.
// Die Einheit ist „Noise-Raum-Einheiten", typischerweise zwischen -2..+2.
struct Point {
    float x;
    float y;
    constexpr Point() : x(0.0f), y(0.0f) {}
    constexpr Point(float x_, float y_) : x(x_), y(y_) {}
};

// --- Motor-Operations-Zustand (State-Machine-Interlock) ---
enum class OpState : uint8_t {
    IDLE        = 0,
    HOMING      = 1,
    CALIBRATING = 2,
    LEARNING    = 3,
    TESTING     = 4,
    SHOWING     = 5,
};

inline const char* opName(OpState s) {
    switch (s) {
        case OpState::IDLE:        return "idle";
        case OpState::HOMING:      return "homing";
        case OpState::CALIBRATING: return "calib";
        case OpState::LEARNING:    return "learn";
        case OpState::TESTING:     return "test";
        case OpState::SHOWING:     return "show";
    }
    return "idle";
}

// --- Pro-Motor-Zustand (live, nicht persistent) ---
struct MotorState {
    bool  enabled  = false;
    float posDeg   = 0.0f;
    float speedSps = 0.0f;
};

// --- Pro-Motor-Kalibrierung (NVS-persistent) ---
//
// FreqSweep-Felder (v4002, derzeit unbenutzt — Reserve für späteren v2):
// pro Frequenzband die zuletzt ermittelte Stall-Amplitude in Steps.
//
// Calib-Skip-Felder (v4003): fastWidthSteps speichert die in der letzten
// erfolgreichen 3-Touch-Calib gemessene P1+P2-Zungenbreite (in Steps bei
// 16 Mikrosteps). Beim nächsten Calib-Lauf wird die frische P1+P2-Messung
// gegen diesen Wert verglichen — Apples-vs-Apples, daher self-consistent
// trotz Sensor-Hysterese und Drehrichtungs-Bias der 3-Touch-Methode.
// 0 = noch unbekannt → kein Skip möglich, fällt auf 3-Touch zurück.
//
// FreqSweep-v3-Felder (v4004): tachoCutoffHz speichert pro Motor die in
// `runTachoCutoffDiagnostic()` ermittelte Frequenz, ab der der induktive
// Tacho keine verlässlichen Pulse mehr liefert (Sensor-Hysterese-Limit).
// 0 = noch nicht gemessen. Phase B (v4.3.3) nutzt diesen Wert als Schwelle,
// ab der SG_RESULT als Fallback-Lebenszeichen herangezogen wird.
struct CalibrationData {
    float    triggerStartDeg  = 0.0f;   // CW edge offset from center [deg]
    float    triggerEndDeg    = 0.0f;   // CCW edge offset [deg]
    float    maxRpm           = 0.0f;
    float    maxAccel         = 0.0f;
    bool     valid            = false;
    uint16_t learnedCurrentMA = 0;
    uint8_t  sgThrs           = 0;
    uint8_t  stableRuns       = 0;
    uint32_t tpwmThrs         = 0;
    uint16_t freqStallAmp[10] = {0,0,0,0,0,0,0,0,0,0};  // (Reserve)
    uint8_t  freqRunCount     = 0;       // (Reserve)
    uint8_t  _pad[1]          = {0};     // explizites Padding für stable layout
    uint16_t fastWidthSteps   = 0;       // P1+P2-Zungenbreite (16 MS), 0=unbekannt
    uint16_t tachoCutoffHz    = 0;       // v4004: f_c [Hz], 0=noch nicht gemessen
    uint16_t _pad2            = 0;       // Alignment für nvsVersion
    uint32_t nvsVersion       = 4004;    // v4004 Schema (FreqSweep-v3 Phase A)
};

} // namespace v4
