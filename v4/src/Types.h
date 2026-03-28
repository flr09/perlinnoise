#pragma once
#include <Arduino.h>

// --- Motor operation state (S2: State-Machine Interlock) ---
enum MotorOpState : uint8_t {
    MOTOR_IDLE        = 0,
    MOTOR_HOMING      = 1,
    MOTOR_CALIBRATING = 2,
    MOTOR_LEARNING    = 3,
    MOTOR_TESTING     = 4,
    MOTOR_SHOWING     = 5
};

struct MotorState {
    float posDeg  = 0;
    float speed   = 0;
    bool  enabled = false;
};

struct CalibrationData {
    float    triggerStartDeg = 0; // CW edge offset from center in degrees
    float    triggerEndDeg   = 0; // CCW edge offset from center in degrees
    float    maxRpm          = 0;
    float    maxAccel        = 0;
    bool     valid           = false;
    uint16_t learnedCurrentMA = 0;
    uint8_t  sgThrs           = 0;
    uint8_t  stableRuns       = 0;
    uint32_t tpwmThrs         = 0;
    uint32_t nvsVersion       = 3619; // NVS structure version
};

struct ParcourConfig {
    bool doSpeed    = true;
    bool doFine     = true;
    bool doAccel    = true;
    bool doCoast    = true;
    bool doKatapult = false;
    bool doFreq     = false;
};

// --- Watchdog-Zustand (Core-0-Task schreibt, Web-Handler liest) ---
struct WatchdogState {
    bool     active      = false;  // Watchdog scharf (nach 5 stabilen Revs)
    bool     triggered   = false;  // Fault ausgelöst (pendingStop gesetzt)
    uint8_t  lastFaultCode = 0;    // Bitmask: bit0=SG, bit1=StepDelta, bit2=Periode
    uint8_t  errorCount  = 0;      // Aufeinanderfolgende fehlerhafte Umdrehungen
    uint8_t  settleCount = 0;      // Aufeinanderfolgende gute Umdrehungen
    long     lastDelta   = 0;      // Letzter Schritt-Delta (für Logging)
    uint32_t lastPeriodMs = 0;     // Letzte gemessene Tacho-Periode (ms)
};

struct SystemState {
    bool         hit     = false;
    MotorOpState opState = MOTOR_IDLE;
    String log = "";
    MotorState      m[4];
    CalibrationData cal[4];
    ParcourConfig   parcour;
    WatchdogState   wd;
    float currentMaxSpd = 4000;
    float currentAccel  = 2000;
    volatile int  pendingHome     = -1;
    volatile int  pendingTest     = -1;
    volatile int  pendingCalib    = -1;
    volatile int  pendingLearn    = -1;
    volatile int  pendingPower    = -1;
    volatile int  pendingKatapult  = -1;
    volatile int  pendingShow      = -1;
    volatile int  pendingFreqSweep  = -1;
    volatile int  pendingCalibTest  = -1;
    volatile bool pendingStop       = false;
};

// --- Plugin sequence types (Freq / Perlin / Oszy) ---
struct FreqEvent    { uint32_t t_ms; float rpm; int8_t dir; };
struct FreqSequence { FreqEvent* events; uint16_t count; bool loop; };
