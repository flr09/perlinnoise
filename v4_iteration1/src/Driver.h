#pragma once
#include <Arduino.h>
#include <TMCStepper.h>
#include <FastAccelStepper.h>
#include "Config.h"
#include "Types.h"

extern TMC2209Stepper      driverX;
extern TMC2209Stepper      driverY;
extern TMC2209Stepper      driverZ;
extern TMC2209Stepper      driverE;
extern FastAccelStepperEngine engine;
extern FastAccelStepper*   stepper;
extern uint16_t            currentMicrosteps;
extern uint16_t            stepsPerRev;

float    rpmToSps(float rpm);
float    spsToRpm(float sps);
uint32_t rpmToTpwmthrs(float rpm);

// --- ANGLE-ABSOLUTE API ---
// degToSteps / stepsToDeg always use the current stepsPerRev.
// After setMicrosteps(), the same physical angle maps to the correct new step count.
// All higher-level modules (Calib, Test, FreqSweep) use these — never stepsPerRev directly.
inline long  degToSteps(float deg)  { return (long)lroundf(deg * (float)stepsPerRev / 360.0f); }
inline float stepsToDeg(long steps) { return (float)steps * 360.0f / (float)stepsPerRev; }

void  moveToDeg(float deg);        // stepper->moveTo(degToSteps(deg))  — non-blocking
void  moveByDeg(float deg);        // stepper->move(degToSteps(deg))    — non-blocking
void  setPositionDeg(float deg);   // stepper->setCurrentPosition(degToSteps(deg))
float getPositionDeg();            // stepsToDeg(stepper->getCurrentPosition())

void initDriver();
void applyDriverSettings(uint16_t runMA);
void setMicrosteps(uint16_t ms);
void setMotorPower(int i, bool on);
