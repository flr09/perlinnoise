#include "Synthesis.h"
#include "RuntimeConfig.h"
#include "NoiseEngine.h"
#include "../../L0_platform/Logger.h"
#include "../../L0_platform/Types.h"
#include "../../L1_hal/Hal_Pins.h"
#include "../../L1_hal/Hal_Output.h"
#include "../../L1_hal/Hal_Tacho.h"
#include "../../L2_storage/Storage_Calib.h"
#include "../../L3_driver/Stepper.h"
#include "../../L3_driver/Tmc2209.h"
#include "../../L3_driver/Units.h"
#include "../../L6_telemetry_safety/OpState.h"
#include "../../L6_telemetry_safety/Recorder.h"
#include "../../L3_driver/Motion.h"
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

// FreqSweep v2 Frequenz-Bänder (MUSS mit Characterization::FREQ_BAND_HZ_V2
// übereinstimmen). Lokal repliziert um L5-cross-Block-Coupling zu vermeiden.
static constexpr uint8_t WAVE_CAP_BANDS = 10;
static constexpr float   WAVE_CAP_FREQ_HZ[WAVE_CAP_BANDS] = {
    6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 20.0f, 26.0f, 36.0f, 50.0f
};

// Lookup max stabile Half-Amp (in MS=16 steps) für gegebene Frequenz f.
// `cal.freqStallAmp[]` wurde im FreqSweep mit MS=64 gemessen → /4 für MS=16.
// Strategie: höchstes Band ≤ f mit valid-Wert (>0) als Referenz, dann
// Extrapolation nach amp ∝ 1/f² (Reversal-Physik). Bei f < lowest band
// wird das niedrigste Band als Referenz genommen.
static long maxAmpAtFreq(const v4::CalibrationData& cal, float f) {
    if (f < 0.01f) return 0;
    int useBand = -1;
    for (int b = WAVE_CAP_BANDS - 1; b >= 0; b--) {
        if (cal.freqStallAmp[b] > 0 && WAVE_CAP_FREQ_HZ[b] <= f) {
            useBand = b; break;
        }
    }
    if (useBand < 0) {
        // f kleiner als alle valid Bänder — nimm das niedrigste valid als Ref
        for (int b = 0; b < WAVE_CAP_BANDS; b++) {
            if (cal.freqStallAmp[b] > 0) { useBand = b; break; }
        }
    }
    if (useBand < 0) return 0;  // gar keine FreqSweep-Daten vorhanden
    float f_ref     = WAVE_CAP_FREQ_HZ[useBand];
    float amp_ref16 = (float)cal.freqStallAmp[useBand] / 4.0f;  // MS=64 → MS=16
    float ratio     = f_ref / f;
    return (long)(amp_ref16 * ratio * ratio);
}

// Cap effSpeed im Wave-Mode so, dass die geforderte Halb-Amplitude
// (effRangeDeg in Synthesis-MS=16-Steps) physikalisch von der Motor-
// Mechanik gehalten wird. Bug-ID 29: ohne diesen Cap führte
// `speed × range × dyn=rasant` zu Geeier am Endpunkt, weil die Halb-
// periode kürzer war als die Reversal-Zeit.
//
// Wenn `demanded > maxAtFreq(f)`: lösen für f_capped via amp ∝ 1/f²:
// f_capped = f * sqrt(maxAtFreq(f) / demanded). Pro Motor das
// restriktivste Limit gewinnt.
static unsigned long lastCapLogMs = 0;
// Sicherheitsfaktor für tachoCutoffHz-basierten Hard-Cap. Phase-A-Lauf 2026-05-09
// zeigte: bei f=fc noch 40 % Pulse, bei f=fc+1 schon 0 % — die Schwelle ist
// scharf. 0.9 lässt den Player knapp unter fc operieren, im verifizierten
// Tacho-Bereich.
static constexpr float WAVE_CAP_TCO_SAFETY = 0.9f;

// Bug 54 (v4.4.13): Cap-Strategie umgekehrt — Speed bleibt User-Wahl, Range
// wird ehrlich auto-gecappt. Vorher: hohe f + große Range → Speed wurde
// runter-gecappt (User sah „Hz drop ohne Grund"). Jetzt: User-f bleibt,
// Range schrumpft auf physikalisch mögliche Amplitude. Match zur User-
// Vorstellung „max speed wert mit Begrenzung der Amplitude".
// Zwei getrennte Caps:
//   (1) `capWaveSpeedByTco`     — Speed nach `tachoCutoffHz` (Sensor-blind).
//   (2) `capWaveRangeByFreqAmp` — Range nach `freqStallAmp` (Amp-Physik).
//
// tachoCutoffHz ist nicht amp-bezogen — Sensor wird oberhalb fc blind egal
// wie klein die Amplitude. Daher bleibt das ein Speed-Cap.

static float capWaveSpeedByTco(float effSpeed) {
    float effSpeedCapped = effSpeed;
    int   restrictedMotor = -1;
    float f = effSpeed / (2.0f * (float)M_PI);
    if (f <= 0.001f) return effSpeed;
    for (int i = 0; i < 4; i++) {
        if (!Stepper::get(i)) continue;
        const auto& cal = calCache[i];
        if (!cal.valid) continue;
        if (cal.tachoCutoffHz > 0) {
            float fTcoMax = (float)cal.tachoCutoffHz * WAVE_CAP_TCO_SAFETY;
            if (f > fTcoMax) {
                float effSpeedTco = fTcoMax * 2.0f * (float)M_PI;
                if (effSpeedTco < effSpeedCapped) {
                    effSpeedCapped = effSpeedTco;
                    restrictedMotor = i;
                }
            }
        }
    }
    if (effSpeedCapped < effSpeed && restrictedMotor >= 0) {
        unsigned long nowMs = millis();
        if (nowMs - lastCapLogMs > 1000) {
            lastCapLogMs = nowMs;
            float fOrig = effSpeed / (2.0f * (float)M_PI);
            float fCap  = effSpeedCapped / (2.0f * (float)M_PI);
            Logger::addLog(String("Speed-Cap M") + v4::motorName(restrictedMotor)
                + " (TCO): " + String(fOrig, 1) + " → " + String(fCap, 1) + " Hz");
        }
    }
    return effSpeedCapped;
}

static unsigned long lastRangeCapLogMs = 0;
static float capWaveRangeByFreqAmp(float effSpeed, float effRangeDeg) {
    float f = effSpeed / (2.0f * (float)M_PI);
    if (f <= 0.001f) return effRangeDeg;
    float effRangeCapped = effRangeDeg;
    int   restrictedMotor = -1;
    for (int i = 0; i < 4; i++) {
        if (!Stepper::get(i)) continue;
        const auto& cal = calCache[i];
        if (!cal.valid) continue;
        long ampMaxSafe = maxAmpAtFreq(cal, f);
        if (ampMaxSafe <= 0) continue;
        long demandedHalfAmp = (long)(effRangeDeg * (float)Stepper::stepsPerRev(i) / 360.0f);
        if (demandedHalfAmp > ampMaxSafe) {
            float maxRangeDeg = (float)ampMaxSafe * 360.0f / (float)Stepper::stepsPerRev(i);
            if (maxRangeDeg < effRangeCapped) {
                effRangeCapped = maxRangeDeg;
                restrictedMotor = i;
            }
        }
    }
    if (effRangeCapped < effRangeDeg && restrictedMotor >= 0) {
        unsigned long nowMs = millis();
        if (nowMs - lastRangeCapLogMs > 1000) {
            lastRangeCapLogMs = nowMs;
            Logger::addLog(String("Range-Cap M") + v4::motorName(restrictedMotor)
                + ": " + (int)effRangeDeg + "° → " + (int)effRangeCapped
                + "° @ " + String(f, 1) + " Hz");
        }
    }
    return effRangeCapped;
}

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

    // accCap pro Modus — feinere Differenzierung für Wave-Modi (Bug-ID 34):
    // Wir nutzen hier die evidenzbasierten Werte aus der Charakterisierung (cal.maxAccel)
    // als Obergrenze. Die Modi definieren nur ihr gewünschtes 'Feeling'.
    bool isSinus  = (v4::rt.moveType == 3);
    bool isSaw    = (v4::rt.moveType == 4);
    bool isSquare = (v4::rt.moveType == 5);

    uint32_t accCap;
    if (stepMode)      accCap = (uint32_t)v4::rt.accelMax;
    else if (isSquare) accCap = 500000;             // Ziel: hart (wird unten durch cal gedeckelt)
    else if (isSaw)    accCap = 100000;             // Ziel: mittel
    else if (isSinus)  accCap = 30000;              // Ziel: agil aber glatt
    else               accCap = DEFAULT_ACC_NOISE;  // Noise (4k)

    // Hardware-Grenzen aus Charakterisierung (Evidenzbasiert)
    // Wenn der Test sagt, der Motor kann nur X, dann fahren wir maximal X.
    if (cal.valid && cal.maxRpm > 0.0f) {
        uint32_t boundSps = (uint32_t)(Units::rpmToSps(i, cal.maxRpm) * SAFETY_FACTOR);
        if (boundSps > 0 && boundSps < spsCap) spsCap = boundSps;
    }
    if (cal.valid && cal.maxAccel > 0.0f) {
        uint32_t boundAcc = (uint32_t)(cal.maxAccel * SAFETY_FACTOR);
        if (boundAcc > HARD_ACCEL_CAP) boundAcc = HARD_ACCEL_CAP;
        
        // Die Hardware-Grenze ist das absolute Limit (Cap nach unten)
        if (boundAcc > 0 && boundAcc < accCap) accCap = boundAcc;
    }

    s->setSpeedInHz(spsCap);
    s->setAcceleration(accCap);
}

// v4.3.1: TPWMTHRS-Hybrid pro Mode für Chopper-Selection.
// StealthChop2 = silent + niedrigeres Drehmoment (für Sinus/Noise ideal).
// SpreadCycle  = lauter + volles Drehmoment (für Square-Sprünge ideal).
// Übergangs-Schwelle bei 500 RPM für Saw/Step (User-Vorgabe 2026-05-06):
// unter 500 RPM = StealthChop, drüber = SpreadCycle.
//   Sinus/Noise/Linear/Circle/Figure8: StealthChop immer (TPWMTHRS=0xFFFFF)
//   Saw/Step/Square:                   Übergang bei 500 RPM (Hybrid)
constexpr float CHOP_SWITCH_RPM = 500.0f; 

static void applyChopperMode(uint8_t i, int moveType) {
    uint32_t tpwm;
    if (moveType >= 4 && moveType <= 6) {
        // Square/Saw/Step: hybrid (StealthChop bei Langsamfahrt, SpreadCycle bei Speed)
        // Wir nutzen hier exakt die 500 RPM Schwelle aus den Projekt-Vorgaben.
        tpwm = Units::rpmToTpwmthrs(i, CHOP_SWITCH_RPM);
    } else {
        // Sinus/Noise/Linear/Circle/Figure8: StealthChop immer (maximal leise)
        tpwm = 0xFFFFF;
    }
    Tmc::setTPWMTHRS(i, tpwm);
}

// v4.3.5: Player-Watchdog — Synthesis-Selbstheilung.
//
// Vergleicht im 3-s-Fenster die tatsächliche Tacho-Pulse-Rate gegen die
// erwartete Rate für den aktuellen Mode. Bei Drift > 50 % stoppt der
// Watchdog die Synthese und loggt — User merkt das im UI (running=false)
// und kann manuell Re-Homen + Re-Starten. Auto-Recovery v2 wäre nice-
// to-have, ist aber komplex (Homing::run blockiert, müsste über
// Op::pending.home asynchron getriggert werden).
//
// Erste Version v1: nur Wave-Mode (moveType 3,4,5) wird überwacht.
// Erwartete Pulses pro Halbschwingung = 2·f Hz × Zeit, gilt aber nur wenn
// die geforderte Halb-Amplitude über der Sensor-Hysterese liegt (~250 Steps
// auf Z, sonst kreuzt der Motor die Sensor-Kante nicht). Bei niedriger f
// oder kleiner amp wird gar nicht überwacht (Pulse-Rate zu langsam für
// statistisch signifikanten Vergleich).
//
// Test-Modi (Calib/Speed/FreqSweep) sind durch v4::rt.running == false
// implizit ausgeschlossen.
static unsigned long lastWdSnapshotMs = 0;
static uint32_t      wdPulsesSnapshot[4] = {0,0,0,0};
static constexpr uint32_t WD_WINDOW_MS  = 3000;
static constexpr float    WD_RATIO_THR  = 0.5f;
static constexpr long     WD_MIN_AMP_STEPS = 250;  // ~Sensor-Hysterese Z
// v4.4.8 Hotfix: WD unter 1 Hz deaktivieren. Bei f<1 Hz frisst die
// Sensor-Hysterese (Bug 32) so viele Pulse, dass das Mathe-Modell
// `expected = 2·f·W` systematisch zu hoch ist → false-positive STOP, auch
// mit adaptivem Fenster. Real-Welt-Beispiel im Log: f=0.5 Hz Square,
// expected=4, real=1, ratio=0.25 → STOP, obwohl Motor mechanisch ok.
// Bei langsamer Bewegung ist ein echter Stall ohnehin nicht
// sicherheitskritisch — Watchdog greift wieder ab 1 Hz.
static constexpr float    WD_MIN_F_HZ      = 1.0f;

static void tickWatchdog() {
    if (!v4::rt.running) {
        lastWdSnapshotMs = 0;
        return;
    }
    bool waveMode = (v4::rt.moveType >= 3 && v4::rt.moveType <= 5);
    if (!waveMode) {
        lastWdSnapshotMs = 0;
        return;
    }

    unsigned long now = millis();

    // Bug 47 (v4.4.8): f früh berechnen, damit das Fenster adaptiv min.
    // zwei Perioden umfasst. Vorher festes 3-s-Fenster → bei f≈0.5 Hz nur
    // 3 erwartete Pulse, eine verpasste Sensorflanke = ratio<0.5 = STOP.
    float effSpeed    = v4::rt.speed * dynSpeed();
    float effRangeDeg = v4::rt.rangeDeg * dynRange();
    effSpeed    = capWaveSpeedByTco(effSpeed);
    effRangeDeg = capWaveRangeByFreqAmp(effSpeed, effRangeDeg);
    float f = effSpeed / (2.0f * (float)M_PI);

    if (lastWdSnapshotMs == 0) {
        for (int i = 0; i < 4; i++) wdPulsesSnapshot[i] = HalTacho::getPulseCount(i);
        lastWdSnapshotMs = now;
        return;
    }

    // Adaptives Fenster: W = max(3 s, 2/f). Garantiert ≥4 erwartete Pulse
    // selbst bei sehr niedrigen Frequenzen — eine einzelne verpasste Flanke
    // kann ratio dann nicht mehr unter 0.5 ziehen.
    uint32_t windowMs = WD_WINDOW_MS;
    if (f > 0.001f) {
        uint32_t twoPeriodsMs = (uint32_t)(2000.0f / f);
        if (twoPeriodsMs > windowMs) windowMs = twoPeriodsMs;
    }
    if (now - lastWdSnapshotMs < windowMs) return;

    if (f < WD_MIN_F_HZ) {
        // Refresh snapshot; zu langsam für sinnvolle Auswertung
        for (int i = 0; i < 4; i++) wdPulsesSnapshot[i] = HalTacho::getPulseCount(i);
        lastWdSnapshotMs = now;
        return;
    }
    float windowS = (float)(now - lastWdSnapshotMs) / 1000.0f;
    float expected = 2.0f * f * windowS;

    for (int i = 0; i < 4; i++) {
        if (!HalPins::hasSensor(i)) continue;
        if (!Stepper::get(i)) continue;
        const auto& cal = calCache[i];
        if (!cal.valid) continue;
        long demandedHalfAmp = (long)(effRangeDeg * (float)Stepper::stepsPerRev(i) / 360.0f);
        if (demandedHalfAmp < WD_MIN_AMP_STEPS) continue;
        // tachoCutoffHz aus Phase A: oberhalb davon erwarten wir keine
        // verlässlichen Pulse mehr (Sensor blind), also kein Drift-Trigger.
        if (cal.tachoCutoffHz > 0 && f > (float)cal.tachoCutoffHz) continue;

        uint32_t real32 = HalTacho::getPulseCount(i) - wdPulsesSnapshot[i];
        float    real   = (float)real32;
        float    ratio  = expected > 0.0f ? (real / expected) : 1.0f;
        if (ratio < WD_RATIO_THR) {
            Logger::addLog(String("PLAYER WD M") + v4::motorName(i)
                + ": ratio=" + String(ratio, 2)
                + " (real=" + (int)real + " exp=" + String(expected, 1)
                + ", f=" + String(f, 1) + " Hz) → STOP");
            stop();
            return;
        }
    }

    // Snapshot-Refresh für nächstes Fenster
    for (int i = 0; i < 4; i++) wdPulsesSnapshot[i] = HalTacho::getPulseCount(i);
    lastWdSnapshotMs = now;
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
        applyChopperMode(i, v4::rt.moveType);
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
            applyChopperMode(i, v4::rt.moveType);
        }
        lastMoveType = v4::rt.moveType;
        Logger::addLog(String("Synth: Mode switch -> ") + lastMoveType);
    }

    float dt = (float)TICK_MS / 1000.0f;
    float effSpeed = v4::rt.speed * dynSpeed();

    // Bug 54 (v4.4.13): Cap-Strategie umgekehrt. Vorher cappte
    // `capWaveSpeedFromFreqStallAmp` die Speed runter, um die User-Range zu
    // halten — User sah „Hz drop ohne Grund". Jetzt: Speed bleibt (nur durch
    // tachoCutoffHz begrenzt, Sensor-blind-Bereich), Range wird auf die bei
    // dieser Frequenz physikalisch tragbare Amplitude geclipped (siehe
    // `effRangeDeg`-Berechnung unten).
    if (v4::rt.moveType >= 3 && v4::rt.moveType <= 5) {
        effSpeed = capWaveSpeedByTco(effSpeed);
    }

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

    // COORDINATE-Modus (moveType=8) — Phase 10 C: statisches Posing.
    // Jeder Motor fährt zu posDeg[i] und hält dort. Kein Sampling, keine
    // Wellenform, keine Synthese — Snapshot-Pose für die Compass-UI.
    // applyEngineCap mit waveMode=false → moderate Accel (Noise-Default).
    if (v4::rt.moveType == 8) {
        for (uint8_t i = 0; i < 4; i++) {
            auto* s = Stepper::get(i);
            if (!s) continue;
            applyEngineCap(i, false, false);
            float stepsPerDeg = (float)Stepper::stepsPerRev(i) / 360.0f;
            long target = (long)(v4::rt.posDeg[i] * stepsPerDeg);
            s->moveTo(target);
        }
        return;
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
    // Bug 54: Range nach FreqAmp-Physik cappen — Wave-Modi nur. Für Noise/
    // Step/etc. bleibt User-Range unverändert (kein Reversal-Limit).
    if (v4::rt.moveType >= 3 && v4::rt.moveType <= 5) {
        effRangeDeg = capWaveRangeByFreqAmp(effSpeed, effRangeDeg);
    }

    for (uint8_t i = 0; i < 4; i++) {
        auto* s = Stepper::get(i);
        if (!s) continue;
        float val = 0.0f;

        if (v4::rt.moveType >= 3) {
            // WAVEFORM. Bug 54 (v4.4.14): contrast wird NICHT angewendet
            // — er ist ein Perlin-Begriff (Hügelgröße im Noise-Raum) und
            // ergibt für reine Wellenformen keinen Sinn. Vorher
            // `val *= contrast` war Altlast aus v2 (User-Bestätigung
            // 2026-05-15: „contrast stumm: der macht hier bei sinus nix").
            // Amplitude wird allein durch `effRangeDeg` skaliert.
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
        } else {
            // NOISE-Modi — Phase 10 C: 2D-Spatial-Sampling. Jeder Motor liest
            // an seiner eigenen (offsetX, offsetY)-Position relativ zum
            // Kamerapfad (flightX/Y). `mspace` skaliert die Offsets in den
            // Noise-Raum (Default-Grid ±1 × mspace ≈ ±25 Einheiten).
            float ox = v4::rt.offsets[i].x * v4::rt.mspace;
            float oy = v4::rt.offsets[i].y * v4::rt.mspace;
            float n = ne.noise((flightX + ox) * v4::rt.framesize,
                                (flightY + oy) * v4::rt.framesize);
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

    // v4.3.5: Player-Watchdog am Ende des Ticks. Misst Tacho-Pulse-Rate
    // gegen erwartete Rate über 3-s-Fenster, stoppt Synthesis bei Drift > 50 %.
    tickWatchdog();

    // Phase 10 G: Recorder-Tick. Eigenes 10 Hz-Throttling intern, hier
    // einfach jedes Mal aufrufen. Liefert Live-Motor-Positionen + Flight-
    // Pfad. No-op wenn nicht recording.
    float posDegLive[4];
    for (uint8_t i = 0; i < 4; i++) posDegLive[i] = Motion::getPositionDeg(i);
    Recorder::tick(flightX, flightY, posDegLive);
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
            // Bug 54: contrast in Wave-Preview ebenfalls deaktiviert (Engine
            // entspricht, val * range übernimmt die Amplitude allein).
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
