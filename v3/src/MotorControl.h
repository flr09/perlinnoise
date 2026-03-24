#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <Arduino.h>
#include <TMCStepper.h>
#include <AccelStepper.h>

// --- HARDWARE PINS (FYSETC E4) ---
#define FW_VERSION "3.1.1-MODULAR"
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

// --- MOTOR STATE & CONFIG ---
struct MotorState {
    float posDeg = 0;
    float speed = 0;
    bool enabled = true;
};

struct CalibrationData {
    long triggerStart = 0;
    long triggerEnd = 0;
    long triggerCenter = 0;
    float maxRpm = 0;
    float maxAccel = 0;
    bool valid = false;
};

struct SystemState {
    bool hit = false;
    String log = "";
    MotorState m[4];
    CalibrationData cal[4];
    float currentMaxSpd = 4000;
    float currentAccel = 2000;
    
    volatile int pendingHome = -1; 
    volatile int pendingTest = -1; 
    volatile int pendingCalib = -1; 
    volatile int pendingInertia = -1; // Fixed: Added missing flag
    volatile bool pendingStop = false;
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
void runSpeedTest(int i);
void runInertiaTest(int i);
void setMotorPower(int i, bool on);
void setMotorDynamics(float speed, float accel);
void emergencyStop();
void saveCalibration(int i);
void loadCalibration();

#endif
