#include "Programs.h"
#include "MotorProfile.h"
#include "Watchdog.h"
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
    if (!sys.cal[0].valid) { addLog("Err: SG-Learn ohne Kalibrierung"); return; }

    setMotorPower(0, true);
    setMicrosteps(16);
    stepper->setAcceleration(5000);
    addLog("SG-Learn...");

    // 600-1200 RPM: SpreadCycle aktiv und SG-Werte stabil eingeschwungen
    // Untergrenze: 600 RPM (laut Doku erst ab hier zuverlaessig)
    // Obergrenze: 80% von maxRpm wenn bekannt, sonst 1200
    float topRpm = (sys.cal[0].maxRpm > 800) ? sys.cal[0].maxRpm * 0.8f : 1200.0f;
    float testRpms[] = { 600.0f, topRpm * 0.6f, topRpm * 0.8f, topRpm };

    uint16_t sgMin = 0xFFFF;
    uint32_t sgSum = 0;
    int samples = 0;

    for (int s = 0; s < 4; s++) {
        if (sys.pendingStop) break;
        stepper->setSpeedInHz((uint32_t)rpmToSps(testRpms[s]));
        stepper->runForward();
        delay(600);  // Einlaufzeit: SG braucht ~500ms zum Einpendeln nach Drehzahlwechsel

        unsigned long t0 = millis();
        while (millis() - t0 < 1500) {
            if (sys.pendingStop) break;
            uint16_t sg = getLatestSgResult();
            if (sg > 0 && sg < sgMin) sgMin = sg;  // Minimum im Freilauf (konservativste Basis)
            sgSum += sg;
            samples++;
            delay(50);
        }
        addLog("SG@" + String((int)testRpms[s]) + "rpm min=" + String(sgMin));
    }

    stepper->stopMove(); waitWhileRunning();

    if (samples > 0 && sgMin < 0xFFFF) {
        // Threshold = 75% des gemessenen Minimums im Freilauf
        // Freilauf-Min ist die konservativste Basis; 75% laesst Spielraum ohne Fehlausloesung
        sys.cal[0].sgThrs = (uint8_t)min(255U, (uint32_t)(sgMin * 0.75f));
        addLog("SG-Thrs: " + String(sys.cal[0].sgThrs) + " (min=" + String(sgMin) + " avg=" + String(sgSum / samples) + ")");
    } else {
        addLog("Err: Keine SG-Daten — Thrs unveraendert");
    }

    saveCalibration(0);
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
}

void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    stepper->setAcceleration(30000);
    float rpm = 300.0f;
    addLog("Speed Test + Profil...");
    profileClear(); // Altes Profil verwerfen — wird neu aufgebaut

    while (rpm <= PARCOUR_RPM_MAX && !sys.pendingStop) {
        stepper->setSpeedInHz((uint32_t)rpmToSps(rpm));
        stepper->runForward();

        // --- Erste 500ms: Einschwingen, Telemetrie ---
        unsigned long tStart = millis();
        while (millis() - tStart < 500) {
            if (sys.pendingStop) break;
            recordDataPoint("SPEED", rpm);
            yield();
        }

        // --- Zweite 500ms: Profildaten sammeln (eingeschwungen) ---
        float pSamples[8]; uint8_t nSamples = 0;
        unsigned long tSettle = millis();
        while (millis() - tSettle < 500) {
            if (sys.pendingStop) break;
            if (nSamples < 8) {
                portENTER_CRITICAL(&motorMux);
                unsigned long p = tachoPeriodMs;
                portEXIT_CRITICAL(&motorMux);
                if (p > 0) pSamples[nSamples++] = (float)p;
            }
            recordDataPoint("SPEED", rpm);
            delay(60); // ~8 Samples bei 500ms
        }

        // Stall-Check
        if (getTachoRpm() < rpm * 0.8f && rpm > 400) {
            addLog("FAIL @ " + String(rpm, 0) + " RPM");
            break;
        }
        sys.cal[0].maxRpm = rpm;

        // Profil-Stützpunkt berechnen (mind. 3 Samples)
        if (nSamples >= 3) {
            float mean = 0.0f;
            for (uint8_t k = 0; k < nSamples; k++) mean += pSamples[k];
            mean /= nSamples;
            float variance = 0.0f;
            for (uint8_t k = 0; k < nSamples; k++) variance += sq(pSamples[k] - mean);
            float sigma = sqrtf(variance / nSamples);
            profileAddPoint(rpm, mean, fmaxf(sigma, 0.5f), getLatestSgResult(), getLatestCsActual());
        }

        rpm += PARCOUR_RPM_STEP;
    }

    stepper->stopMove(); waitWhileRunning();
    saveCalibration(0);
    if (motorProfile.valid) {
        profileSave();
        addLog("Profil: " + String(motorProfile.count) + " Punkte gespeichert");
    }
    setMicrosteps(64);
    applyDriverSettings(MOTOR_CURRENT_DEFAULT);
    addLog("Speed DONE.");
}

void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    setMotorPower(0, true);
    setMicrosteps(16);
    addLog("Inertia Test...");
    float acc = 1000; bool failed = false;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 400.0f;
    stepper->setSpeedInHz((uint32_t)rpmToSps(testSpd));
    while(acc <= 40000 && !failed && !sys.pendingStop) {
        stepper->setAcceleration(acc);
        stepper->move(stepsPerRev * 2); if(!waitWhileRunning()) break;
        stepper->move(-stepsPerRev * 2); if(!waitWhileRunning()) break;
        // Simple check: if we didn't crash/timeout, we increase
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
    
    // Cut power
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
