#include "Synthesis.h"
#include "RuntimeConfig.h"
#include "NoiseEngine.h"
#include "../../L0_platform/Logger.h"
#include "../../L0_platform/Types.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L2_storage/Storage_Calib.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L3_driver/Units.h"
#include "../../L6_telemetry_safety/OpState.h"
#include <math.h>

namespace v4 { RuntimeConfig rt; }

namespace Synthesis {

// Eine einzige NoiseEngine-Instanz für Engine + Preview. Hält die SimplexNoise-
// Permutation, sodass tick() und getPreviewBytes() konsistent dieselbe
// Noise-Map sehen.
static NoiseEngine ne;
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

// Engine-Cap aus NVS-Charakterisierung. Wenn Motor nicht kalibriert ist
// (cal.valid = false → noch keine SpeedTest/Inertia gelaufen) gilt der
// konservative Hardcoded-Default. 95%-Marge zur gemessenen Stall-Grenze.
static constexpr float SAFETY_FACTOR = 0.95f;
static constexpr uint32_t DEFAULT_SPS_NOISE  = 8000;
static constexpr uint32_t DEFAULT_ACC_NOISE  = 4000;
static constexpr uint32_t DEFAULT_SPS_STEP   = 20000;

static void applyEngineCap(uint8_t i, bool stepMode) {
    auto* s = Stepper::get(i);
    if (!s) return;
    v4::CalibrationData cal;
    StorageCalib::load(i, cal);

    uint32_t spsCap = stepMode ? DEFAULT_SPS_STEP : DEFAULT_SPS_NOISE;
    uint32_t accCap = stepMode ? (uint32_t)v4::rt.accelMax : DEFAULT_ACC_NOISE;

    if (cal.valid && cal.maxRpm > 0.0f) {
        uint32_t boundSps = (uint32_t)(Units::rpmToSps(i, cal.maxRpm) * SAFETY_FACTOR);
        if (boundSps > 0 && (stepMode ? boundSps < spsCap : true)) spsCap = boundSps;
    }
    if (cal.valid && cal.maxAccel > 0.0f) {
        uint32_t boundAcc = (uint32_t)(cal.maxAccel * SAFETY_FACTOR);
        if (boundAcc > 0 && boundAcc < accCap) accCap = boundAcc;
    }

    s->setSpeedInHz(spsCap);
    s->setAcceleration(accCap);
}

void start() {
    if (v4::rt.running) return;
    unsigned long now = millis();
    bool stepMode = (v4::rt.moveType == 6);
    // Power für alle 4 Motoren
    for (uint8_t i = 0; i < 4; i++) {
        Tmc::setPower(i, true);
        Stepper::setMicrosteps(i, 16);
        applyEngineCap(i, stepMode);
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
                    // STEP-Modus liest accelMax-Slider live, aber capped via Engine-Cap
                    // (setSpeedInHz wurde in start() gesetzt und bleibt stehen).
                    applyEngineCap(i, true);
                    s->moveTo((long)(target * (float)Stepper::stepsPerRev(i) / 360.0f));
                    st.isMoving = true;
                }
            }
        }
        return;
    }

    // 2) Pro Motor Ziel-Position berechnen.
    // Hinweis: Noise-Math hier verwendet den klassischen `i*mspace`-Offset
    // aus V1 (asymmetrisch, ankert bei i=0). NoiseEngine::getVal() bietet
    // alternativ einen um die 4-Motor-Mitte zentrierten Offset — bewusst
    // hier nicht genutzt, um das Verhalten gegenüber V1 nicht zu verändern.
    float effRangeDeg = v4::rt.rangeDeg * dynRange();

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
            float n = ne.noise((flightX + (float)i * v4::rt.mspace) * v4::rt.framesize,
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

size_t getPreviewBytes(uint8_t* out, size_t maxBytes) {
    const auto& c = v4::rt;
    if (c.moveType <= 2) {
        // 32x32-Grid um die aktuelle Engine-Position. Skala = framesize, damit
        // die Visualisierung in derselben "Optik" arbeitet wie die Engine.
        const int W = 32, H = 32;
        if (maxBytes < (size_t)(W * H)) return 0;
        const float fs = c.framesize > 0.0f ? c.framesize : 0.01f;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float u = (flightX + (float)(x - W/2)) * fs;
                float v = (flightY + (float)(y - H/2)) * fs;
                float n = ne.noise(u, v);
                int g = (int)((n + 1.0f) * 127.5f);
                if (g < 0) g = 0; else if (g > 255) g = 255;
                out[y * W + x] = (uint8_t)g;
            }
        }
        return W * H;
    }
    if (c.moveType <= 5) {
        // 1D-Plot einer Wellenform-Periode + Marker für aktuelle Phase.
        // Marker = Bit 7 gesetzt am Phasen-Index; Sample-Wert in Bits 0..6
        // (0..127, halbiert). Browser entpackt entsprechend.
        const int N = 128;
        if (maxBytes < (size_t)N) return 0;
        float curPhase = fmodf(timeAcc / (2.0f * (float)M_PI), 1.0f);
        if (curPhase < 0.0f) curPhase += 1.0f;
        int markerIdx = (int)(curPhase * N);
        for (int i = 0; i < N; i++) {
            float t = (float)i / (float)N;
            float val = 0.0f;
            if (c.moveType == 3) {                  // SINUS
                val = sinf(t * 2.0f * (float)M_PI);
            } else if (c.moveType == 4) {           // SAWTOOTH
                float exp = fmaxf(0.1f, expf(c.zShape * 0.25f));
                val = powf(t, exp) * 2.0f - 1.0f;
            } else {                                 // SQUARE
                float duty = fmaxf(0.05f, fminf(0.95f, 0.5f + c.zShape * 0.08f));
                val = t < duty ? 1.0f : -1.0f;
            }
            val *= c.contrast;
            if (val < -1.0f) val = -1.0f; else if (val > 1.0f) val = 1.0f;
            int b = (int)((val + 1.0f) * 63.5f);    // 0..127
            if (b < 0) b = 0; else if (b > 127) b = 127;
            uint8_t byte = (uint8_t)b;
            if (i == markerIdx) byte |= 0x80;       // Marker-Bit
            out[i] = byte;
        }
        return N;
    }
    return 0;  // STEP-Mode oder unbekannt: keine Visualisierung
}

} // namespace Synthesis
