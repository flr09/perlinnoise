#include "Watchdog.h"
#include "OpState.h"
#include "MotorProfile.h"
#include "Telemetry.h"
#include "../L0_platform/Logger.h"
#include "../L0_platform/Sync.h"
#include "../L0_platform/Types.h"
#include "../L1_hal/Hal_Pins.h"
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

    // Tacho-Pulse erkennen
    uint32_t currentPulseCount = HalTacho::getPulseCount(i);
    uint32_t numNewPulses = currentPulseCount - lastSeenPulse[i];
    if (numNewPulses == 0) return;     // kein neuer Puls seit letztem Tick
    lastSeenPulse[i] = currentPulseCount;

    long curPos = s->getCurrentPosition();
    long deltaTotal = curPos - lastSeenStepperPos[i];
    lastSeenStepperPos[i] = curPos;

    long deltaPerPulse = deltaTotal / (long)numNewPulses;
    uint32_t periodUs = HalTacho::tacho[i].periodUs;

    state[i].lastDeltaPerPulse = deltaPerPulse;
    state[i].lastPeriodUs = periodUs;

    if (firstRev[i]) {
        firstRev[i] = false;
        return;   // erste Umdrehung dient nur als Baseline
    }

    float rpm = Units::spsToRpm(i, (float)(s->getCurrentSpeedInMilliHz() / 1000));
    if (rpm < 200.0f) {
        state[i].settleCount = 0;
        return;
    }

    // Drehzahländerung > 10% → re-arm
    if (state[i].active && fabsf(rpm - lastArmedRpm[i]) > lastArmedRpm[i] * 0.10f) {
        state[i].active = false;
        state[i].settleCount = 0;
        state[i].errorCount  = 0;
        lastArmedRpm[i] = 0.0f;
    }

    // Evidence A: Tacho-Periode vs. Profil
    bool evidA = false;
    float pMeanUs, pSigmaUs;
    if (MotorProfileNs::getExpected(i, rpm, &pMeanUs, &pSigmaUs)) {
        // MotorProfile nutzt intern nun auch Us
        float dev = fabsf((float)periodUs - pMeanUs);
        evidA = dev > 3.5f * pSigmaUs; // etwas toleranter
    }

    // Evidence B: Schritt-Delta pro Puls (sollte stepsPerRev sein)
    long expected = (long)Stepper::stepsPerRev(i);
    bool evidB = expected > 0 && labs(labs(deltaPerPulse) - expected) > expected / 8;

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
                               String(faultCode, BIN) + " d=" + deltaPerPulse + " p=" + (periodUs/1000) + "ms");
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

// Tacho-Polling läuft jetzt in eigenem 200Hz-Task (Hal_Tacho::init()),
// nicht mehr im Watchdog-Tick — sonst werden Sensor-Durchquerungen bei
// hoher Acc verfehlt (~30ms Zungen-Zeit < 50ms Tick).

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
