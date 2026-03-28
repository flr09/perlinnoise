#include "Programs.h"
#include "MotorControl.h"
#include "Driver.h"
#include "Sensor.h"
#include "Telemetry.h"
#include "Types.h"
#include "Config.h"

extern SystemState sys;
extern FastAccelStepper *stepper;

// Hilfsfunktion für sicheres Warten in Programmen
bool waitWhileRunning(unsigned long timeoutMs = 0) {
    unsigned long start = millis();
    while (stepper->isRunning()) {
        if (sys.pendingStop) { stepper->stopMove(); return false; }
        if (timeoutMs > 0 && millis() - start > timeoutMs) { stepper->stopMove(); return false; }
        yield();
    }
    return true;
}

void learnSGProfile(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("SG-Learn...");
    float testRpms[] = {150, 250, 350, 450};
    float sgSum = 0; int samples = 0;
    for(int s=0; s<4; s++) {
        if (sys.pendingStop) break;
        stepper->setSpeedInHz((uint32_t)rpmToSps(testRpms[s]));
        stepper->runForward();
        unsigned long t0 = millis();
        while(millis() - t0 < 1500) {
            if (sys.pendingStop) break;
            sgSum += (float)getLatestSgResult(); samples++;
            delay(50);
        }
    }
    stepper->stopMove(); waitWhileRunning();
    if(samples > 0) sys.cal[0].sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
    saveCalibration(0);
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("SG-Thrs: " + String(sys.cal[0].sgThrs));
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    stepper->setAcceleration(30000);
    float rpm = 300.0f;
    addLog("Speed Test...");
    while(rpm <= PARCOUR_RPM_MAX && !sys.pendingStop) {
        stepper->setSpeedInHz((uint32_t)rpmToSps(rpm));
        stepper->runForward();
        unsigned long tStart = millis();
        while(millis() - tStart < 1000) {
            if (sys.pendingStop) break;
            recordDataPoint("SPEED", rpm);
            yield();
        }
        if (getTachoRpm() < rpm * 0.8f && rpm > 400) {
            addLog("FAIL @ " + String(rpm,0) + " RPM");
            break;
        }
        sys.cal[0].maxRpm = rpm;
        rpm += PARCOUR_RPM_STEP;
    }
    stepper->stopMove(); waitWhileRunning();
    saveCalibration(0);
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("Speed DONE.");
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Inertia Test...");
    float acc = 1000;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 400.0f;
    stepper->setSpeedInHz((uint32_t)rpmToSps(testSpd));
    while(acc <= 40000 && !sys.pendingStop) {
        stepper->setAcceleration(acc);
        stepper->move(stepsPerRev * 2); if(!waitWhileRunning()) break;
        stepper->move(-stepsPerRev * 2); if(!waitWhileRunning()) break;
        acc += 2000;
    }
    sys.cal[0].maxAccel = acc - 2000;
    saveCalibration(0);
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("Inertia DONE.");
}

void runCoastTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Coast Test...");
    stepper->setSpeedInHz((uint32_t)rpmToSps(400));
    stepper->runForward();
    delay(1000);
    if (sys.pendingStop) { stepper->stopMove(); return; }
    
    setMotorPower(0, false);
    stepper->stopMove();
    unsigned long t0 = millis();
    uint32_t p0 = getPulseCount();
    while(millis() - t0 < 2000) { recordDataPoint("COAST", 0); yield(); }
    uint32_t p1 = getPulseCount();
    addLog("Coast Pulses: " + String(p1 - p0));
    
    setMotorPower(0, true);
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
}

void runKatapult(int i) {
    if (i != 0) return;
    setMotorPower(0, true);
    addLog("KATAPULT START");
    float launchRpm = sys.cal[0].maxRpm > 1000 ? sys.cal[0].maxRpm : 2000;
    stepper->setAcceleration(100000);
    stepper->setSpeedInHz((uint32_t)rpmToSps(launchRpm));
    
    for(int r=0; r<3; r++) {
        if(sys.pendingStop) break;
        stepper->runForward();
        unsigned long t0 = millis();
        while(millis() - t0 < 1500) { recordDataPoint("KAT_CW", launchRpm); yield(); }
        stepper->stopMove(); waitWhileRunning();
        delay(200);
    }
    addLog("KATAPULT DONE");
}

void runFreqSweep(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(64);
    addLog("Freq Sweep...");
    
    unsigned long tStart = millis();
    float tDur_ms = FREQ_SWEEP_S * 1000.0f;
    int dir = 1;
    
    while (!sys.pendingStop) {
        float elapsed = (float)(millis() - tStart);
        float progress = elapsed / tDur_ms;
        if (progress >= 1.0f) break;

        float f = FREQ_MIN_HZ + (FREQ_MAX_HZ - FREQ_MIN_HZ) * progress;
        float amp_f = (float)FREQ_ACCEL_MAX / (16.0f * f * f);
        long  amp = (long)min(amp_f * 0.9f, (float)stepsPerRev/4.0f);
        if (amp < FREQ_AMP_MIN_STEPS) break;

        stepper->setSpeedInHz((uint32_t)sqrtf((float)FREQ_ACCEL_MAX * (float)amp));
        stepper->setAcceleration(FREQ_ACCEL_MAX);
        stepper->moveTo(dir * amp);
        while (stepper->isRunning()) { recordDataPoint("FS", f); yield(); }
        dir = -dir;
    }
    moveToDeg(0); waitWhileRunning();
    addLog("Freq Sweep DONE.");
}

void runPerformanceShow(int i) {
    addLog("Show: Agility...");
    runSpeedTest(0);
    if(sys.pendingStop) return;
    addLog("Show: Resonanz...");
    runFreqSweep(0);
    addLog("VORFÜHRUNG BEENDET");
}
