#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// --- HARDWARE PINS (FYSETC E4) ---
#define FW_VERSION "3.3.6"
#define R_SENSE 0.11f
#define ENABLE_PIN 25
#define SERIAL_PORT Serial2
#define UART_RX 21
#define UART_TX 22
#define TACHO_PIN 15

#define X_STEP 27
#define X_DIR  26
#define Y_STEP 33
#define Y_DIR  32
#define Z_STEP 14
#define Z_DIR  12
#define E_STEP 16
#define E_DIR  17

// --- MOTOR LIMITS ---
#define MOTOR_CURRENT_MIN_MA   300
#define MOTOR_CURRENT_MAX_MA   900
#define MOTOR_CURRENT_DEFAULT  650
#define MOTOR_SGTHRS_DEFAULT    50

// --- PARCOUR RPM BEREICH ---
#define PARCOUR_RPM_START   300.0f
#define PARCOUR_RPM_MAX    2500.0f
#define PARCOUR_RPM_STEP     100.0f
#define PARCOUR_RPM_FINE      10.0f

// --- MOTOR STATE & CONFIG ---
struct MotorState {
    float posDeg = 0;
    float speed = 0;
    bool enabled = true;
};

struct CalibrationData {
    long triggerStart  = 0;
    long triggerEnd    = 0;
    long triggerCenter = 0;
    float maxRpm       = 0;
    float maxAccel     = 0;
    bool valid         = false;
    uint16_t learnedCurrentMA = 0;
    uint8_t  sgThrs           = 0;
    uint8_t  stableRuns       = 0;
    uint32_t tpwmThrs         = 0;
};

struct ParcourConfig {
    bool doSpeed = true;
    bool doFine  = true;
    bool doAccel = true;
    bool doCoast = true;
};

struct SystemState {
    bool hit = false;
    String log = "";
    MotorState m[4];
    CalibrationData cal[4];
    ParcourConfig parcour;
    volatile int pendingHome    = -1;
    volatile int pendingTest    = -1;
    volatile int pendingCalib   = -1;
    volatile int pendingLearn   = -1;
    volatile bool pendingStop   = false;
};

extern SystemState sys;
extern portMUX_TYPE motorMux;

// --- STEPPER OBJECTS ---
extern TMC2209Stepper driverX;
extern AccelStepper stX;
extern AccelStepper* steppers[4];

// --- FUNCTIONS ---
void initMotors();
void updateMotors();
void homeMotor(int i);
void characterizeSensor(int i);
void learnSGProfile(int i);
void runCoastTest(int i);
void runSpeedTest(int i);
void runInertiaTest(int i);
void setMotorPower(int i, bool on);
void addLog(String msg);

// --- TELEMETRY ---
#define TELEM_INTERVAL_MS 50
#define TELEM_MAX_BYTES   52000
extern String telemCSV;
extern unsigned long telemStart;
void clearTelemetry();
void recordTelemetry(const char* phase, float val);
void updateTelemCache();

#endif
