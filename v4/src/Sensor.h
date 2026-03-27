#pragma once
#include <Arduino.h>
#include "Config.h"
#include "driver/pcnt.h"

// PCNT-based position capture at sensor edge (ISR-safe, no Flash access).
// lastSensorRaw + pcntStepperBase = absolute FAS position at sensor edge.
extern volatile long     lastSensorRaw;
extern volatile bool     sensorHit;
extern long              pcntStepperBase;

// Tacho RPM tracking (one LOW edge per revolution)
extern volatile unsigned long tachoPeriodMs;
extern volatile unsigned long lastTachoLowMs;
extern volatile uint32_t      pulseCount;

void     initSensor();
uint16_t getTachoRpm();
uint32_t getPulseCount();
bool     waitForSensorTimed(bool state, long maxSteps, unsigned long timeoutMs);
bool     waitForSensorStable(bool state, long maxSteps, unsigned long timeoutMs);
bool     checkSensorStable(bool targetState, int requiredHits);

// --- LATCH API ---
void sensorLatchReset();
void enablePcntInputBuffer();
