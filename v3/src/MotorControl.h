#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// --- HARDWARE PINS (FYSETC E4) ---
#define FW_VERSION "3.3.0"
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

#define LAMP_PIN 2
#define FAN_PIN 13

// --- MOTOR LIMITS (motors/pancake/datasheet.md) ---
#define MOTOR_CURRENT_MIN_MA   300
#define MOTOR_CURRENT_MAX_MA   900   // 36BYG1204: ~0.92A Nenn, max 0.90A RMS für Sicherheit
#define MOTOR_CURRENT_DEFAULT  650   // Orbiter-Empfehlung: 0.85A RMS Startpunkt
#define MOTOR_SGTHRS_DEFAULT    50

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
    // --- adaptives Tuning ---
    uint16_t learnedCurrentMA = 0;  // IRUN: 0 → MOTOR_CURRENT_DEFAULT
    uint8_t  sgThrs           = 0;  // 0 → noch nicht gelernt
    uint8_t  stableRuns       = 0;  // aufeinanderfolgende stabile Läufe
    uint32_t tpwmThrs         = 0;  // 0 → default (200 RPM Schwelle)
};

struct ParcourConfig {
    bool doSpeed = true;   // Geschwindigkeits-Parcour (100 RPM Stufen)
    bool doFine  = true;   // Feinjustierung (10 RPM Stufen nach Grob-Fail)
    bool doAccel = true;   // Trägheitstest (steigende Beschleunigung)
    bool doCoast = true;   // Ausroll-Test (Burst + Freilauf + Sensor-Check)
};

struct SystemState {
    bool hit = false;
    String log = "";
    MotorState m[4];
    CalibrationData cal[4];
    ParcourConfig parcour;
    float currentMaxSpd = 4000;
    float currentAccel = 2000;

    volatile int pendingHome    = -1;
    volatile int pendingTest    = -1;
    volatile int pendingCalib   = -1;
    volatile int pendingInertia = -1;
    volatile int pendingLearn   = -1;
    volatile bool pendingStop   = false;
};

extern SystemState sys;
extern portMUX_TYPE motorMux;

// --- STEPPER OBJECTS ---
extern TMC2209Stepper driverX, driverY, driverZ, driverE;
extern AccelStepper stX, stY, stZ, stE;
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
void setMotorDynamics(float speed, float accel);
void emergencyStop();
void saveCalibration(int i);
void loadCalibration();

// --- TELEMETRY ---
#define TELEM_INTERVAL_MS 200
#define TELEM_MAX_BYTES   52000
extern String telemCSV;
extern unsigned long telemStart;
void clearTelemetry();
void recordTelemetry(const char* phase, float val);

#endif
