#pragma once
#include <Arduino.h>

struct MotorState {
    float posDeg  = 0;
    float speed   = 0;
    bool  enabled = false;
};

struct CalibrationData {
    long     triggerStart  = 0;
    long     triggerEnd    = 0;
    long     triggerCenter = 0;
    float    maxRpm        = 0;
    float    maxAccel      = 0;
    bool     valid         = false;
    uint16_t learnedCurrentMA = 0;
    uint8_t  sgThrs           = 0;
    uint8_t  stableRuns       = 0;
    uint32_t tpwmThrs         = 0;
};

struct ParcourConfig {
    bool doSpeed    = true;
    bool doFine     = true;
    bool doAccel    = true;
    bool doCoast    = true;
    bool doKatapult = false;
    bool doFreq     = false;
};

struct SystemState {
    bool   hit = false;
    String log = "";
    MotorState      m[4];
    CalibrationData cal[4];
    ParcourConfig   parcour;
    float currentMaxSpd = 4000;
    float currentAccel  = 2000;
    volatile int  pendingHome     = -1;
    volatile int  pendingTest     = -1;
    volatile int  pendingCalib    = -1;
    volatile int  pendingLearn    = -1;
    volatile int  pendingPower    = -1;
    volatile int  pendingKatapult  = -1;
    volatile int  pendingShow      = -1;
    volatile int  pendingFreqSweep = -1;
    volatile bool pendingStop      = false;
};

// --- Plugin sequence types (Freq / Perlin / Oszy) ---
struct FreqEvent    { uint32_t t_ms; float rpm; int8_t dir; };
struct FreqSequence { FreqEvent* events; uint16_t count; bool loop; };
