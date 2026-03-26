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

void initDriver();
void applyDriverSettings(uint16_t runMA);
void setMicrosteps(uint16_t ms);
void setMotorPower(int i, bool on);
