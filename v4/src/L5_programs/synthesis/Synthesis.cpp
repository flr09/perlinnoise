#include "Synthesis.h"
#include "RuntimeConfig.h"
#include "NoiseEngine.h"
#include "../../L0_platform/Logger.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L6_telemetry_safety/OpState.h"
#include <math.h>

namespace v4 { RuntimeConfig rt; }

namespace Synthesis {

static SimplexNoise sn;
static float flightX = 0.0f;
static float flightY = 0.0f;
static float timeAcc = 0.0f;
static unsigned long lastTickMs = 0;
static const unsigned int TICK_MS = 10;  // 100 Hz Update

// STEP-Mode-State pro Motor: aktueller Schritt (0..3), Hold-Start-Zeit, isMoving-Flag
struct StepState {
    uint8_t  currentStep = 0;
    bool     isMoving    = false;
    unsigned long holdStartMs = 0;
};
static StepState stepState[4];

// Drive-Dynamics-Skalierungen aus V1 MANUAL.md
static float dynSpeed() {
    switch (v4::rt.dynamics) {
        case 0: return 0.60f;
        case 2: return 1.65f;
        default: return 1.0f;
    }
}
static float dynRange() {
    switch (v4::rt.dynamics) {
        case 0: return 0.75f;
        case 2: return 1.30f;
        default: return 1.0f;
    }
}

void init() {
    // SimplexNoise-Konstruktor läuft bereits beim Modulelaufzeit.
    // Hier nur reset der Pfad-Variablen.
    flightX = flightY = timeAcc = 0.0f;
    lastTickMs = 0;
}

void start() {
    if (v4::rt.running) return;
    unsigned long now = millis();
    // Power für alle 4 Motoren
    for (uint8_t i = 0; i < 4; i++) {
        Tmc::setPower(i, true);
        Stepper::setMicrosteps(i, 16);
        auto* s = Stepper::get(i);
        if (s) {
            // Im STEP-Mode höhere Acceleration nutzen — sonst smoother Default
            if (v4::rt.moveType == 6) {
                s->setSpeedInHz(20000);
                s->setAcceleration(v4::rt.accelMax);
            } else {
                s->setSpeedInHz(8000);
                s->setAcceleration(4000);
            }
        }
        // STEP-State zurücksetzen. holdStartMs auf jetzt setzen, damit der
        // erste Hold-Zyklus an Position 0° (currentStep=0) beginnt — sonst
        // springt der Motor ohne Wartezeit direkt auf 90° (currentStep=1).
        stepState[i] = StepState();
        stepState[i].holdStartMs = now;
    }
    flightX = flightY = timeAcc = 0.0f;
    v4::rt.running = true;
    Logger::addLog(String("Synth: START type=") + v4::rt.moveType);
}

void stop() {
    if (!v4::rt.running) return;
    for (uint8_t i = 0; i < 4; i++) {
        auto* s = Stepper::get(i);
        if (s) s->stopMove();
    }
    v4::rt.running = false;
    Logger::addLog("Synth: STOP");
}

void tick() {
    if (!v4::rt.running) return;
    unsigned long now = millis();
    if (now - lastTickMs < TICK_MS) return;
    lastTickMs = now;

    float dt = (float)TICK_MS / 1000.0f;
    float effSpeed = v4::rt.speed * dynSpeed();

    // 1) Pfad-Generator
    if (v4::rt.moveType == 0) {            // LINEAR
        float rad = v4::rt.angle * (float)M_PI / 180.0f;
        flightX += cosf(rad) * effSpeed * dt * 10.0f;
        flightY += sinf(rad) * effSpeed * dt * 10.0f;
        const float wrap = 1000000.0f;
        flightX = fmodf(flightX, wrap);
        flightY = fmodf(flightY, wrap);
    } else if (v4::rt.moveType <= 2) {     // CIRCLE / FIGURE8
        timeAcc += effSpeed * dt;
        if (v4::rt.moveType == 1) {
            flightX = v4::rt.radius * cosf(timeAcc);
            flightY = v4::rt.radius * sinf(timeAcc);
        } else {
            flightX = v4::rt.radius * cosf(timeAcc);
            flightY = (v4::rt.radius * 0.5f) * sinf(timeAcc * 2.0f);
        }
    } else {                                // WAVEFORM 3..5
        timeAcc += effSpeed * dt;
    }

    // STEP-Modus (moveType=6): 4-Position-Quader-Drehung mit Hold-Time.
    // Andere Logik als Noise/Wellenform — wird hier separat gehandhabt und
    // dann return.
    if (v4::rt.moveType == 6) {
        unsigned long nowMs = millis();
        float effHoldMs = v4::rt.holdMs / dynSpeed();  // Dynamics skaliert Hold (Rasant=schneller)
        for (uint8_t i = 0; i < 4; i++) {
            auto* s = Stepper::get(i);
            if (!s) continue;
            auto& st = stepState[i];
            if (st.isMoving) {
                // FAS noch unterwegs — warten bis Ziel erreicht
                if (!s->isRunning()) {
                    st.isMoving = false;
                    st.holdStartMs = nowMs;
                }
            } else {
                // Hold-Phase — wenn abgelaufen, nächster Schritt
                if ((unsigned long)(nowMs - st.holdStartMs) >= (unsigned long)effHoldMs) {
                    st.currentStep = (st.currentStep + 1) % 4;
                    // Pro-Motor-Phasen-Offset via mspace: bei mspace=25 → 90° versetzt
                    float phaseOff = (float)i * (v4::rt.mspace / 100.0f) * 360.0f;
                    float target = (float)st.currentStep * v4::rt.stepAngle
                                 + (float)st.currentStep * v4::rt.stepOffset
                                 + phaseOff;
                    s->setAcceleration(v4::rt.accelMax);
                    s->setSpeedInHz(20000);  // hoch — limitiert ohnehin durch Acceleration über kurze Strecke
                    s->moveTo((long)(target * (float)Stepper::stepsPerRev(i) / 360.0f));
                    st.isMoving = true;
                }
            }
        }
        return;
    }

    // 2) Pro Motor Ziel-Position berechnen
    float effRangeDeg = v4::rt.rangeDeg * dynRange();
    NoiseConfig nc;
    nc.framesize = v4::rt.framesize;
    nc.contrast  = v4::rt.contrast;
    nc.zShape    = v4::rt.zShape;
    nc.edgeC     = v4::rt.edgeC;

    for (uint8_t i = 0; i < 4; i++) {
        auto* s = Stepper::get(i);
        if (!s) continue;
        float val = 0.0f;

        if (v4::rt.moveType >= 3) {
            // WAVEFORM
            float phaseSpread = (v4::rt.mspace / 100.0f) * 2.0f * (float)M_PI;
            float phase = timeAcc + (float)i * phaseSpread;
            if (v4::rt.moveType == 3) {            // SINUS
                val = sinf(phase);
            } else if (v4::rt.moveType == 4) {     // SAWTOOTH
                float t = fmodf(phase / (2.0f * (float)M_PI), 1.0f);
                if (t < 0.0f) t += 1.0f;
                float exp = fmaxf(0.1f, expf(v4::rt.zShape * 0.25f));
                val = powf(t, exp) * 2.0f - 1.0f;
            } else {                                // SQUARE
                float t = fmodf(phase / (2.0f * (float)M_PI), 1.0f);
                if (t < 0.0f) t += 1.0f;
                float duty = fmaxf(0.05f, fminf(0.95f, 0.5f + v4::rt.zShape * 0.08f));
                val = t < duty ? 1.0f : -1.0f;
            }
            val *= v4::rt.contrast;
        } else {
            // NOISE-Modi: pro Motor mit Spacing-Offset
            float n = sn.noise((flightX + (float)i * v4::rt.mspace) * v4::rt.framesize,
                                flightY * v4::rt.framesize);
            float nNorm = (n + 1.0f) * 0.5f;
            float exp = fmaxf(0.1f, fabsf(v4::rt.zShape));
            float nShaped = powf(nNorm, exp);
            if (v4::rt.zShape < 0.0f) nShaped = 1.0f - nShaped;
            val = (nShaped * 2.0f - 1.0f) * v4::rt.contrast;
        }

        // 3) Skalierung auf Schritte (Stepper hat bereits Microsteps eingestellt)
        float stepsPerDeg = (float)Stepper::stepsPerRev(i) / 360.0f;
        long maxSteps = (long)(effRangeDeg * stepsPerDeg);
        long target = (long)(val * (float)maxSteps);
        if (target > maxSteps)  target = maxSteps;
        if (target < -maxSteps) target = -maxSteps;
        s->moveTo(target);
    }
}

} // namespace Synthesis
