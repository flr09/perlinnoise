#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "Config.h"
#include "Types.h"
#include "IModule.h"
#include "Sensor.h"
#include "Driver.h"
#include "Telemetry.h"

extern SystemState        sys;
extern portMUX_TYPE       motorMux;
extern SemaphoreHandle_t  uartMutex;

// --- CORE FUNCTIONS ---
void initMotors();
void updateMotors();
void addLog(String msg);

// --- CALIB / HOMING (→ Calib plugin, Phase 3) ---
void homeMotor(int i);
void characterizeSensor(int i);
void learnSGProfile(int i);
void gotoCardinal();

// --- TEST FUNCTIONS (→ Test plugin, Phase 6) ---
void runCoastTest(int i);
void runSpeedTest(int i);
void runInertiaTest(int i);
void runKatapult(int i);
void runPerformanceShow(int i);
void runFreqSweep(int i);

#endif
