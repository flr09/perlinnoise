#pragma once
#include <Arduino.h>

// L0 — Daten-Typen, layer-übergreifend genutzt.
//
// Schichten-Regel: L2 (Storage) muss CalibrationData kennen, dürfte aber nicht
// auf L4 zugreifen — also liegen die reinen Daten-Typen hier in L0.

namespace v4 {

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
    uint32_t nvsVersion       = 4001;   // v4 Schema (war 3619 in v3)
};

} // namespace v4
