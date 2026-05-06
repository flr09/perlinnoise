#include "Synthesis.h"
#include "RuntimeConfig.h"
#include "NoiseEngine.h"
#include "../../L0_platform/Logger.h"
#include "../../L0_platform/Types.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L1_hal/Hal_Output.h"
#include "../../L2_storage/Storage_Calib.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L3_driver/Units.h"
#include "../../L6_telemetry_safety/OpState.h"
#include <math.h>

namespace v4 { RuntimeConfig rt; }

namespace Synthesis {

// Eine einzige NoiseEngine-Instanz für Engine + Preview.
static NoiseEngine ne;
static float flightX = 0.0f;
static float flightY = 0.0f;
static float timeAcc = 0.0f;
static unsigned long lastTickMs = 0;
static const unsigned int TICK_MS = 10;  // 100 Hz Update

// Trackt den Modus beim letzten Tick, um bei Modus-Wechsel die Hardware-Caps
// (Accel/Speed) reaktiv anzupassen.
static int lastMoveType = -1;

// STEP-Mode-State pro Motor
struct StepState {
    uint8_t  currentStep = 0;
    bool     isMoving    = false;
    unsigned long holdStartMs = 0;
};
static StepState stepState[4];

// Cache für Motor-Grenzen, um NVS-Zugriffe im 100Hz-Tick zu vermeiden.
static v4::CalibrationData calCache[4];

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
    HalOutput::init();
    flightX = flightY = timeAcc = 0.0f;
    lastTickMs = 0;
}

static int lastFanWritten  = -1;
static int lastLampWritten = -1;

static void pushOutputs() {
    int curFan  = (int)v4::rt.fan;
    int curLamp = v4::rt.lamp > 0 ? 1 : 0;
    if (curFan != lastFanWritten) {
        HalOutput::setFan((uint8_t)curFan);
        lastFanWritten = curFan;
    }
    if (curLamp != lastLampWritten) {
        HalOutput::setLamp(curLamp != 0);
        lastLampWritten = curLamp;
    }
}

// Engine-Capping & Performance
static constexpr float SAFETY_FACTOR = 0.95f;
static constexpr uint32_t DEFAULT_SPS_NOISE  = 8000;
static constexpr uint32_t DEFAULT_ACC_NOISE  = 4000;
static constexpr uint32_t DEFAULT_SPS_STEP   = 20000;
static constexpr uint32_t HARD_ACCEL_CAP     = 500000; // Schutz für Mechanik

static void applyEngineCap(uint8_t i, bool stepMode, bool waveMode) {
    auto* s = Stepper::get(i);
    if (!s) return;
    const auto& cal = calCache[i];

    // spsCap: Ziel-Geschwindigkeit je Modus
    uint32_t spsCap;
    if (stepMode)      spsCap = DEFAULT_SPS_STEP;
    else if (waveMode) spsCap = 40000; // Waves dürfen schneller als Noise (8k)
    else               spsCap = DEFAULT_SPS_NOISE;

    // accCap: Beschleunigung je Modus
    uint32_t accCap;
    if (stepMode)      accCap = (uint32_t)v4::rt.accelMax;
    else if (waveMode) accCap = 100000; // Initialer Wave-Default
    else               accCap = DEFAULT_ACC_NOISE;

    // Hardware-Grenzen aus Charakterisierung (Evidenzbasiert)
    if (cal.valid && cal.maxRpm > 0.0f) {
        uint32_t boundSps = (uint32_t)(Units::rpmToSps(i, cal.maxRpm) * SAFETY_FACTOR);
        // Wir deckeln den Modus-Default durch das physikalische Limit.
        if (boundSps > 0 && boundSps < spsCap) spsCap = boundSps;
    }
    if (cal.valid && cal.maxAccel > 0.0f) {
        uint32_t boundAcc = (uint32_t)(cal.maxAccel * SAFETY_FACTOR);
        if (boundAcc > HARD_ACCEL_CAP) boundAcc = HARD_ACCEL_CAP;
        
        if (waveMode) {
            // Wave-Mode (Square/Saw): wir WOLLEN so hart wie möglich springen.
            // Also nutzen wir die volle Hardware-Kapazität als festen Wert.
            accCap = boundAcc;
        } else {
            // Step+Noise: Hardware-Grenze ist nur der Deckel für den User-Slider
            // bzw. den konservativen Noise-Default.
            if (boundAcc > 0 && boundAcc < accCap) accCap = boundAcc;
        }
    }

    s->setSpeedInHz(spsCap);
    s->setAcceleration(accCap);
}

void start() {
    if (v4::rt.running) return;
    unsigned long now = millis();
    bool stepMode = (v4::rt.moveType == 6);
    bool waveMode = (v4::rt.moveType >= 3 && v4::rt.moveType <= 5);
    for (uint8_t i = 0; i < 4; i++) {
        Tmc::setPower(i, true);
        Stepper::setMicrosteps(i, 16);
        // NVS-Grenzen cachen
        StorageCalib::load(i, calCache[i]);
        applyEngineCap(i, stepMode, waveMode);
        stepState[i] = StepState();
        stepState[i].holdStartMs = now;
    }
    flightX = flightY = timeAcc = 0.0f;
    lastMoveType = v4::rt.moveType;
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
    unsigned long now = millis();
    if (now - lastTickMs < TICK_MS) return;
    lastTickMs = now;

    pushOutputs();

    if (!v4::rt.running) return;

    // KRITISCH (Fix ID 24/29): Reaktiv auf Modus-Wechsel reagieren.
    // Wenn der User von Noise auf Square schaltet, müssen Accel/Speed
    // neu berechnet und an die Stepper gepusht werden.
    if (v4::rt.moveType != lastMoveType) {
        bool stepMode = (v4::rt.moveType == 6);
        bool waveMode = (v4::rt.moveType >= 3 && v4::rt.moveType <= 5);
        for (uint8_t i = 0; i < 4; i++) {
            applyEngineCap(i, stepMode, waveMode);
        }
        lastMoveType = v4::rt.moveType;
        Logger::addLog(String("Synth: Mode switch -> ") + lastMoveType);
    }

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

    // STEP-Modus (moveType=6)
    if (v4::rt.moveType == 6) {
        unsigned long nowMs = millis();
        float effHoldMs = v4::rt.holdMs / dynSpeed();
        for (uint8_t i = 0; i < 4; i++) {
            auto* s = Stepper::get(i);
            if (!s) continue;
            auto& st = stepState[i];
            if (st.isMoving) {
                if (!s->isRunning()) {
                    st.isMoving = false;
                    st.holdStartMs = nowMs;
                }
            } else {
                if ((unsigned long)(nowMs - st.holdStartMs) >= (unsigned long)effHoldMs) {
                    st.currentStep = (st.currentStep + 1) % 4;
                    float phaseOff = (float)i * (v4::rt.mspace / 100.0f) * 360.0f;
                    float target = (float)st.currentStep * v4::rt.stepAngle
                                 + (float)st.currentStep * v4::rt.stepOffset
                                 + phaseOff;
                    applyEngineCap(i, true, false);
                    s->moveTo((long)(target * (float)Stepper::stepsPerRev(i) / 360.0f));
                    st.isMoving = true;
                }
            }
        }
        return;
    }

    // 2) Pro Motor Ziel-Position berechnen
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
            // NOISE-Modi
            float n = ne.noise((flightX + (float)i * v4::rt.mspace) * v4::rt.framesize,
                                flightY * v4::rt.framesize);
            // KRITISCH (Fix ID 25): applyShape nutzt nun auch edgeC
            val = ne.applyShape(n, v4::rt.zShape, v4::rt.edgeC) * v4::rt.contrast;
        }

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
        const int W = 32, H = 32;
        if (maxBytes < (size_t)(W * H)) return 0;
        const float fs = c.framesize > 0.0f ? c.framesize : 0.01f;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float u = (flightX + (float)(x - W/2)) * fs;
                float v = (flightY + (float)(y - H/2)) * fs;
                float n = ne.noise(u, v);
                // KRITISCH (Fix ID 25b): Preview nutzt identisches Shaping
                float shaped = ne.applyShape(n, c.zShape, c.edgeC) * c.contrast;
                int g = (int)((shaped + 1.0f) * 127.5f);
                if (g < 0) g = 0; else if (g > 255) g = 255;
                out[y * W + x] = (uint8_t)g;
            }
        }
        return W * H;
    }
    if (c.moveType <= 5) {
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
            int b = (int)((val + 1.0f) * 63.5f);
            if (b < 0) b = 0; else if (b > 127) b = 127;
            uint8_t byte = (uint8_t)b;
            if (i == markerIdx) byte |= 0x80;
            out[i] = byte;
        }
        return N;
    }
    return 0;
}

} // namespace Synthesis
