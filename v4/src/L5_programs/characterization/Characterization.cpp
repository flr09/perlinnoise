#include "Characterization.h"
#include "../../L0_platform/Logger.h"
#include "../../L0_platform/Types.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L1_hal/Hal_Tacho.h"
#include "../../L2_storage/Storage_Calib.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L3_driver/Motion.h"
#include "../../L3_driver/Units.h"
#include "../../L6_telemetry_safety/OpState.h"
#include "../../L6_telemetry_safety/Telemetry.h"

namespace Characterization {

constexpr uint16_t PARCOUR_RPM_MAX  = 2500;
constexpr uint16_t PARCOUR_RPM_STEP = 100;

constexpr float    FREQ_MIN_HZ      = 10.0f;
constexpr float    FREQ_MAX_HZ      = 200.0f;
constexpr float    FREQ_SWEEP_S     = 15.0f;
constexpr uint32_t FREQ_ACCEL_MAX   = 500000UL;
constexpr long     FREQ_AMP_MIN     = 5;

static bool waitOrStop(uint8_t i, unsigned long timeoutMs = 30000) {
    return Motion::waitWhileRunning(i, &Op::pendingStop, timeoutMs);
}

void runSgLearn(uint8_t i) {
    if (i >= 4) return;
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    Logger::addLog(String("M") + (char)('X'+i) + ": SG-Learn");
    auto* s = Stepper::get(i); if (!s) return;
    float testRpms[] = {150, 250, 350, 450};
    float sgSum = 0; int samples = 0;
    for (int k = 0; k < 4; k++) {
        if (Op::pendingStop) break;
        s->setSpeedInHz((uint32_t)Units::rpmToSps(i, testRpms[k]));
        s->runForward();
        unsigned long t0 = millis();
        while (millis() - t0 < 1500) {
            if (Op::pendingStop) break;
            sgSum += (float)Telemetry::getLatestSg(i);
            samples++;
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    s->stopMove(); waitOrStop(i);
    if (samples > 0) {
        v4::CalibrationData cal;
        StorageCalib::load(i, cal);
        cal.sgThrs = (uint8_t)(sgSum / (float)samples * 0.6f);
        StorageCalib::save(i, cal);
        Logger::addLog(String("SG-Thrs: ") + cal.sgThrs);
    }
    Stepper::setMicrosteps(i, 64);
    Tmc::applyDefaults(i);
}

void runSpeedTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("SpeedTest: not calibrated"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i); if (!s) return;
    s->setAcceleration(30000);
    Logger::addLog(String("M") + (char)('X'+i) + ": Speed Test");
    float rpm = 300.0f;
    while (rpm <= PARCOUR_RPM_MAX && !Op::pendingStop) {
        s->setSpeedInHz((uint32_t)Units::rpmToSps(i, rpm));
        s->runForward();
        unsigned long t0 = millis();
        while (millis() - t0 < 1000) {
            if (Op::pendingStop) break;
            Telemetry::recordDataPoint(i, "SPEED", rpm);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (HalTacho::getRpm(i) < rpm * 0.8f && rpm > 400) {
            Logger::addLog(String("FAIL @") + (int)rpm + " RPM"); break;
        }
        cal.maxRpm = rpm;
        rpm += PARCOUR_RPM_STEP;
    }
    s->stopMove(); waitOrStop(i);
    StorageCalib::save(i, cal);
    Stepper::setMicrosteps(i, 64);
    Tmc::applyDefaults(i);
}

void runInertiaTest(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("Inertia: not calibrated"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i); if (!s) return;
    Logger::addLog("Inertia Test");
    float acc = 1000;
    float testSpd = cal.maxRpm > 0 ? cal.maxRpm * 0.7f : 400.0f;
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, testSpd));
    while (acc <= 40000 && !Op::pendingStop) {
        s->setAcceleration(acc);
        s->move(Stepper::stepsPerRev(i) * 2); if (!waitOrStop(i)) break;
        s->move(-(long)Stepper::stepsPerRev(i) * 2); if (!waitOrStop(i)) break;
        Telemetry::recordDataPoint(i, "INERT", acc);
        acc += 2000;
    }
    cal.maxAccel = acc - 2000;
    StorageCalib::save(i, cal);
    Stepper::setMicrosteps(i, 64);
    Tmc::applyDefaults(i);
}

void runCoastTest(uint8_t i) {
    if (i >= 4) return;
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i); if (!s) return;
    Logger::addLog("Coast Test");
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, 400));
    s->runForward();
    delay(1000);
    if (Op::pendingStop) { s->stopMove(); return; }
    Tmc::setPower(i, false);
    s->stopMove();
    unsigned long t0 = millis();
    uint32_t p0 = HalTacho::getPulseCount(i);
    while (millis() - t0 < 2000) {
        Telemetry::recordDataPoint(i, "COAST", 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    uint32_t p1 = HalTacho::getPulseCount(i);
    Logger::addLog(String("Coast pulses: ") + (p1 - p0));
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 64);
    Tmc::applyDefaults(i);
}

void runKatapult(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    Tmc::setPower(i, true);
    auto* s = Stepper::get(i); if (!s) return;
    Logger::addLog("KATAPULT");
    float launchRpm = cal.maxRpm > 1000 ? cal.maxRpm : 2000;
    s->setAcceleration(100000);
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, launchRpm));
    for (int r = 0; r < 3; r++) {
        if (Op::pendingStop) break;
        s->runForward();
        unsigned long t0 = millis();
        while (millis() - t0 < 1500) {
            Telemetry::recordDataPoint(i, "KAT", launchRpm);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        s->stopMove(); waitOrStop(i);
        delay(200);
    }
}

void runFreqSweep(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("FreqSweep: not calibrated"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 64);
    auto* s = Stepper::get(i); if (!s) return;
    Logger::addLog("Freq Sweep");
    unsigned long tStart = millis();
    float tDur = FREQ_SWEEP_S * 1000.0f;
    int dir = 1;
    while (!Op::pendingStop) {
        float elapsed = (float)(millis() - tStart);
        float progress = elapsed / tDur;
        if (progress >= 1.0f) break;
        float f = FREQ_MIN_HZ + (FREQ_MAX_HZ - FREQ_MIN_HZ) * progress;
        float ampF = (float)FREQ_ACCEL_MAX / (16.0f * f * f);
        long amp = (long)min(ampF * 0.9f, (float)Stepper::stepsPerRev(i) / 4.0f);
        if (amp < FREQ_AMP_MIN) break;
        s->setSpeedInHz((uint32_t)sqrtf((float)FREQ_ACCEL_MAX * (float)amp));
        s->setAcceleration(FREQ_ACCEL_MAX);
        s->moveTo(dir * amp);
        while (s->isRunning()) {
            if (Op::pendingStop) { s->stopMove(); break; }
            Telemetry::recordDataPoint(i, "FS", f);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        dir = -dir;
    }
    Motion::moveToDeg(i, 0.0f);
    waitOrStop(i);
}

void runCurrentSweepHiRPM(uint8_t i) {
    if (i >= 4) return;
    v4::CalibrationData cal; StorageCalib::load(i, cal);
    if (!cal.valid) { Logger::addLog("CurrentSweep: not calibrated"); return; }
    Tmc::setPower(i, true);
    Stepper::setMicrosteps(i, 16);
    auto* s = Stepper::get(i); if (!s) return;
    Logger::addLog("CurrentSweep @ HiRPM (B-EMF Sweet-Spot)");
    float rpm = cal.maxRpm > 1000 ? cal.maxRpm * 0.9f : 2200.0f;
    s->setAcceleration(50000);
    s->setSpeedInHz((uint32_t)Units::rpmToSps(i, rpm));
    s->runForward();
    delay(500);
    for (uint16_t mA = 1200; mA >= 200 && !Op::pendingStop; mA -= 100) {
        Tmc::applyDefaults(i, mA);
        unsigned long t0 = millis();
        while (millis() - t0 < 800) {
            Telemetry::recordDataPoint(i, "CS_HI", mA);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        Logger::addLog(String("mA=") + mA + " realRpm=" + HalTacho::getRpm(i));
    }
    s->stopMove(); waitOrStop(i);
    Stepper::setMicrosteps(i, 64);
    Tmc::applyDefaults(i);
}

void runPerformanceShow(uint8_t i) {
    Logger::addLog("Show: Speed");
    runSpeedTest(i);
    if (Op::pendingStop) return;
    Logger::addLog("Show: FreqSweep");
    runFreqSweep(i);
    Logger::addLog("Vorführung Ende");
}

} // namespace Characterization
