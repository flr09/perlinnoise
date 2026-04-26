#include "Watchdog.h"
#include "OpState.h"
#include "MotorProfile.h"
#include "Telemetry.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Types.h"
#include "../L1_hal/Hal_Tacho.h"
#include "../L2_storage/Storage_Calib.h"
#include "../L3_driver/Stepper.h"
#include "../L3_driver/Units.h"
#include <math.h>

namespace Watchdog {

State state[4];
static bool enabled[4] = { false, false, false, false };
static uint32_t lastSeenPulse[4] = { 0, 0, 0, 0 };
static long     lastSeenStepperPos[4] = { 0, 0, 0, 0 };
static float    lastArmedRpm[4] = { 0, 0, 0, 0 };
static bool     firstRev[4] = { true, true, true, true };

void init() {
    for (uint8_t i = 0; i < 4; i++) {
        state[i] = State();
        enabled[i] = false;
        firstRev[i] = true;
        lastSeenPulse[i] = 0;
        lastSeenStepperPos[i] = 0;
        lastArmedRpm[i] = 0.0f;
    }
}

void enable(uint8_t i, bool on) {
    if (i >= 4) return;
    enabled[i] = on;
    firstRev[i] = true;
    if (!on) state[i] = State();
}

static void tickMotor(uint8_t i) {
    if (!enabled[i]) return;
    auto* s = Stepper::get(i);
    if (!s) return;

    // Tacho-Pulse-Edge erkennen
    uint32_t puls = HalTacho::getPulseCount(i);
    if (puls == lastSeenPulse[i]) return;     // keine neue Umdrehung
    lastSeenPulse[i] = puls;

    long curPos = s->getCurrentPosition();
    long delta = curPos - lastSeenStepperPos[i];
    lastSeenStepperPos[i] = curPos;

    // Tacho-Periode ist in HalTacho gespeichert
    uint32_t periodMs = HalTacho::tacho[i].periodMs;

    state[i].lastDelta    = delta;
    state[i].lastPeriodMs = periodMs;

    if (firstRev[i]) {
        firstRev[i] = false;
        return;   // erste Umdrehung dient nur als Baseline
    }

    float rpm = Units::spsToRpm(i, (float)(s->getCurrentSpeedInMilliHz() / 1000));
    if (rpm < 200.0f) {
        state[i].settleCount = 0;
        return;
    }

    // Drehzahländerung > 8% → re-arm
    if (state[i].active && fabsf(rpm - lastArmedRpm[i]) > lastArmedRpm[i] * 0.08f) {
        state[i].active = false;
        state[i].settleCount = 0;
        state[i].errorCount  = 0;
        lastArmedRpm[i] = 0.0f;
    }

    // Evidence A: Tacho-Periode vs. Profil
    bool evidA = false;
    float pMean, pSigma;
    if (MotorProfileNs::getExpected(i, rpm, &pMean, &pSigma)) {
        float dev = fabsf((float)periodMs - pMean);
        evidA = dev > 3.0f * pSigma;
    }

    // Evidence B: Schritt-Delta
    long expected = (long)Stepper::stepsPerRev(i);
    bool evidB = expected > 0 && labs(labs(delta) - expected) > expected / 10;

    // Evidence C: SG_RESULT unter Threshold
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    bool evidC = cal.sgThrs > 0 && Telemetry::getLatestSg(i) < cal.sgThrs;

    int faultCount = (int)evidA + (int)evidB + (int)evidC;
    uint8_t faultCode = ((uint8_t)evidA << 2) | ((uint8_t)evidB << 1) | (uint8_t)evidC;

    if (faultCount >= 2) {
        if (state[i].active) {
            state[i].errorCount++;
            state[i].lastFaultCode = faultCode;
            state[i].settleCount = 0;
            if (state[i].errorCount >= 3) {
                state[i].triggered = true;
                state[i].active    = false;
                Op::pendingStop = true;
                Logger::addLog(String("WD M") + (char)('X'+i) + " FAULT 0b" +
                               String(faultCode, BIN) + " d=" + delta + "/" + expected +
                               " p=" + periodMs + "ms sg=" + Telemetry::getLatestSg(i));
            }
        }
        state[i].settleCount = 0;
    } else {
        state[i].errorCount = 0;
        state[i].settleCount++;
        if (!state[i].active && state[i].settleCount >= 5) {
            state[i].active = true;
            state[i].triggered = false;
            lastArmedRpm[i] = rpm;
            Logger::addLog(String("WD M") + (char)('X'+i) + " armed @" + (int)rpm + "rpm");
        }
    }
}

void tick() {
    for (uint8_t i = 0; i < 4; i++) tickMotor(i);
}

static void watchdogTask(void*) {
    for (;;) {
        tick();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void startTask() {
    xTaskCreatePinnedToCore(watchdogTask, "WD", 3072, nullptr, 1, nullptr, 0);
}

} // namespace Watchdog
