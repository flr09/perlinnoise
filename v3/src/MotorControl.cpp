#include "MotorControl.h"
#include <Preferences.h>

Preferences prefs;
SystemState sys;
portMUX_TYPE motorMux = portMUX_INITIALIZER_UNLOCKED;

TMC2209Stepper driverX(&SERIAL_PORT, R_SENSE, 1);
TMC2209Stepper driverY(&SERIAL_PORT, R_SENSE, 3);
TMC2209Stepper driverZ(&SERIAL_PORT, R_SENSE, 0);
TMC2209Stepper driverE(&SERIAL_PORT, R_SENSE, 2);

AccelStepper stX(AccelStepper::DRIVER, X_STEP, X_DIR);
AccelStepper stY(AccelStepper::DRIVER, Y_STEP, Y_DIR);
AccelStepper stZ(AccelStepper::DRIVER, Z_STEP, Z_DIR);
AccelStepper stE(AccelStepper::DRIVER, E_STEP, E_DIR);
AccelStepper* steppers[4] = {&stX, &stY, &stZ, &stE};

float rpmToSps(float rpm)  { return (rpm * 3200.0f) / 60.0f; }

// Geschwindigkeit → TPWMTHRS Register (TMC2209 interner Takt 12 MHz)
// StealthChop aktiv wenn TSTEP > TPWMTHRS (= unter dieser Drehzahl)
uint32_t rpmToTpwmthrs(float rpm) {
    float usps = rpm * 3200.0f / 60.0f; // Mikroschritte/s
    if (usps < 1.0f) return 0xFFFFF;
    return (uint32_t)(12000000.0f / usps);
}

// --- TELEMETRY ---
String telemCSV = "";
unsigned long telemStart = 0;
static unsigned long lastTelemMs = 0;

void clearTelemetry() {
    telemCSV = "ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,cur_a,cur_b,stall,otpw,ot,ola,olb\n";
    telemStart = millis();
    lastTelemMs = 0;
}

void recordTelemetry(const char* phase, float val) {
    unsigned long now = millis();
    if (telemCSV.length() > TELEM_MAX_BYTES) return;
    if (now - lastTelemMs < TELEM_INTERVAL_MS) return;
    lastTelemMs = now;

    long pos = steppers[0]->currentPosition();
    int  spd = (int)steppers[0]->speed();
    uint16_t sg = driverX.SG_RESULT();
    uint8_t  cs = driverX.cs_actual();
    uint32_t msc = driverX.MSCURACT();
    int16_t cur_a = (int16_t)(msc & 0x1FF);         if (cur_a > 255) cur_a -= 512;
    int16_t cur_b = (int16_t)((msc >> 16) & 0x1FF); if (cur_b > 255) cur_b -= 512;

    // Stall: SG unter gelernte Schwelle (oder 0 wenn noch nicht gelernt)
    uint8_t thrs = sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : 0;
    uint8_t flags = (sg <= thrs ? 1 : 0) | (driverX.otpw() ? 2 : 0)
                  | (driverX.ot()   ? 4 : 0) | (driverX.ola() ? 8 : 0) | (driverX.olb() ? 16 : 0);

    char line[128];
    snprintf(line, sizeof(line), "%lu,%s,%.0f,%ld,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d\n",
        now - telemStart, phase, val, pos, spd, sg, cs, cur_a, cur_b,
        (flags>>0)&1, (flags>>1)&1, (flags>>2)&1, (flags>>3)&1, (flags>>4)&1);
    telemCSV += line;
}

// --- HELPERS ---
void addLog(String msg) {
    portENTER_CRITICAL(&motorMux);
    sys.log += msg + "\\n";
    portEXIT_CRITICAL(&motorMux);
    Serial.println(msg);
}

uint16_t activeCurrent(int i) {
    uint16_t c = sys.cal[i].learnedCurrentMA;
    return (c >= MOTOR_CURRENT_MIN_MA && c <= MOTOR_CURRENT_MAX_MA) ? c : MOTOR_CURRENT_DEFAULT;
}

void applyDriverSettings(uint16_t runMA) {
    // IRUN + IHOLD: 25 % Haltestrom (leise im Stillstand), min 200 mA
    float holdFrac = min(0.5f, max(0.22f, 200.0f / (float)runMA));
    driverX.rms_current(runMA, holdFrac);
    driverX.iholddelay(10);       // ~300 ms bis Haltestrom aktiv

    // StallGuard-Schwelle
    uint8_t thrs = sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT;
    driverX.SGTHRS(thrs);

    // Automatische StealthChop↔SpreadCycle Umschaltung per Geschwindigkeit
    // Unter tpwmThrs (Default: 200 RPM) → StealthChop (leise, Haltestrom)
    // Über tpwmThrs               → SpreadCycle (Drehmoment, Laufstrom)
    uint32_t tpwm = sys.cal[0].tpwmThrs > 0
                    ? sys.cal[0].tpwmThrs
                    : rpmToTpwmthrs(200);
    driverX.TPWMTHRS(tpwm);
    driverX.en_spreadCycle(false); // Automatik via TPWMTHRS

    // StallGuard erst ab ~50 RPM auswerten (darunter unzuverlässig)
    driverX.TCOOLTHRS(rpmToTpwmthrs(50));

    // PWM-Autotuning für optimale StealthChop-Effizienz
    driverX.pwm_autograd(true);
    driverX.pwm_autoscale(true);
}

// --- CALIBRATION PERSISTENCE ---
void saveCalibration(int i) {
    prefs.begin("calib", false);
    String key = "m" + String(i);
    prefs.putBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    prefs.end();
}

void loadCalibration() {
    prefs.begin("calib", true);
    for (int i = 0; i < 4; i++) {
        String key = "m" + String(i);
        if (prefs.isKey(key.c_str()))
            prefs.getBytes(key.c_str(), &sys.cal[i], sizeof(CalibrationData));
    }
    prefs.end();
}

// --- MOTOR POWER ---
void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        driverX.begin();
        driverX.toff(5);
        driverX.microsteps(16);
        driverX.pwm_autoscale(true);
        applyDriverSettings(activeCurrent(0));
        digitalWrite(ENABLE_PIN, LOW);
        addLog("M0 ON (" + String(activeCurrent(0)) + "mA, SGTHRS=" + String(sys.cal[0].sgThrs) + ")");
    } else {
        digitalWrite(ENABLE_PIN, HIGH);
        addLog("M0 OFF");
    }
}

void setSpreadCycle(int i, bool enable) {
    if (i == 0) driverX.en_spreadCycle(enable);
}

// --- INIT ---
void initMotors() {
    loadCalibration();
    SERIAL_PORT.begin(115200, SERIAL_8N1, UART_RX, UART_TX);

    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(ENABLE_PIN, LOW);

    driverX.begin();
    driverX.toff(5);
    driverX.microsteps(16);
    driverX.pwm_autoscale(true);
    applyDriverSettings(activeCurrent(0));

    pinMode(TACHO_PIN, INPUT_PULLUP);

    steppers[0]->setMaxSpeed(4000);
    steppers[0]->setAcceleration(2000);
}

// --- SENSOR HELPERS ---
bool waitForSensor(int i, bool state, unsigned long timeoutMs) {
    unsigned long start = millis();
    while (digitalRead(TACHO_PIN) != state) {
        if (millis() - start > timeoutMs) return false;
        steppers[i]->runSpeed();
        yield();
    }
    return true;
}

// --- HOME ---
void homeMotor(int i) {
    if (i != 0) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    addLog("Homing M0...");
    setSpreadCycle(0, true);
    steppers[0]->setSpeed(1200);
    if (!waitForSensor(0, LOW, 15000)) { addLog("Err: M0 Timeout!"); return; }

    steppers[0]->setSpeed(-400);
    waitForSensor(0, HIGH, 3000);
    steppers[0]->setSpeed(100);
    waitForSensor(0, LOW, 3000);

    steppers[0]->setCurrentPosition(0);
    long target = sys.cal[0].valid ? sys.cal[0].triggerCenter : 0;
    steppers[0]->moveTo(target);
    unsigned long mS = millis();
    while (steppers[0]->distanceToGo() != 0 && millis() - mS < 5000) { steppers[0]->run(); yield(); }
    setSpreadCycle(0, false);
    addLog("M0 Home.");
}

// --- CALIB SENSOR ---
void characterizeSensor(int i) {
    if (i != 0) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    addLog("Mapping M0...");
    setSpreadCycle(0, true);

    steppers[0]->setSpeed(150);
    if (!waitForSensor(0, LOW, 15000)) { addLog("Err: CW fail"); return; }
    long sCW = steppers[0]->currentPosition();
    if (!waitForSensor(0, HIGH, 5000)) { addLog("Err: CW End fail"); return; }
    long eCW = steppers[0]->currentPosition();

    steppers[0]->move(3200); while (steppers[0]->run()) { yield(); }
    steppers[0]->setSpeed(-150);
    if (!waitForSensor(0, LOW, 25000)) { addLog("Err: CCW fail"); return; }
    long eCCW = steppers[0]->currentPosition();
    if (!waitForSensor(0, HIGH, 5000)) { addLog("Err: CCW End fail"); return; }
    long sCCW = steppers[0]->currentPosition();

    sys.cal[0].triggerCenter = ((sCW + eCW) / 2 + (sCCW + eCCW) / 2) / 2;
    sys.cal[0].valid = true;
    saveCalibration(0);
    addLog("M0 Center: " + String(sys.cal[0].triggerCenter));
    homeMotor(0);
}

// --- SG LERNLAUF ---
// Misst SG_RESULT in 5 Geschwindigkeitsstufen:
// - Bestimmt SGTHRS (60% des Mittelwerts im SpreadCycle-Bereich)
// - Findet Übergangsgeschwindigkeit StealthChop→SpreadCycle (TPWMTHRS)
//   → Punkt, ab dem SG stabil wird (SpreadCycle liefert brauchbare SG-Werte)
void learnSGProfile(int i) {
    if (i != 0) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);

    uint16_t cur = activeCurrent(0);
    addLog("SG-Lernlauf @" + String(cur) + "mA...");
    applyDriverSettings(cur); // inkl. TPWMTHRS, TCOOLTHRS, IHOLD

    // Messpunkte: sps → zugehörige RPM und SG-Mittelwert
    struct { uint32_t sps; uint32_t sgAvg; } pts[] = {
        {200,0}, {500,0}, {1000,0}, {2000,0}, {4000,0}
    };
    const int N = 5;

    for (int s = 0; s < N; s++) {
        steppers[0]->setMaxSpeed(pts[s].sps);
        steppers[0]->setAcceleration(pts[s].sps * 4);
        steppers[0]->move(3200);

        uint32_t sgSum = 0; uint32_t n = 0, nS = 0;
        while (steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            if (n % 20 == 0) { sgSum += driverX.SG_RESULT(); nS++; }
            n++; yield();
        }
        pts[s].sgAvg = nS > 0 ? sgSum / nS : 0;
        float rpm = pts[s].sps * 60.0f / 3200.0f;
        addLog("SG @" + String(rpm,0) + "RPM (" + String(pts[s].sps) + "sps): " + String(pts[s].sgAvg));
    }

    // TPWMTHRS-Kandidat: erste Stufe, ab der SG deutlich > 0 und stabil ist
    // (StallGuard liefert erst im SpreadCycle-Bereich sinnvolle Werte)
    uint32_t tpwmCandidate = rpmToTpwmthrs(200); // Default
    for (int s = 1; s < N; s++) {
        if (pts[s].sgAvg > 10 && pts[s].sgAvg > pts[s-1].sgAvg * 2) {
            // Sprunghafter SG-Anstieg → hier beginnt SpreadCycle wirklich zu greifen
            float rpm = pts[s].sps * 60.0f / 3200.0f;
            tpwmCandidate = rpmToTpwmthrs(rpm * 0.8f); // 20% Hysterese
            addLog("TPWMTHRS-Sprung bei " + String(rpm, 0) + " RPM");
            break;
        }
    }

    // SGTHRS: 60% des Mittelwerts der oberen Hälfte (SpreadCycle-Bereich)
    uint32_t sgHigh = 0; int n = 0;
    for (int s = N/2; s < N; s++) { sgHigh += pts[s].sgAvg; n++; }
    uint32_t sgMean = n > 0 ? sgHigh / n : MOTOR_SGTHRS_DEFAULT * 2;
    uint8_t thrs = (uint8_t)min(255UL, max(1UL, (sgMean * 60UL) / 100UL));

    sys.cal[0].sgThrs   = thrs;
    sys.cal[0].tpwmThrs = tpwmCandidate;
    applyDriverSettings(cur); // nochmal mit gelernten Werten
    saveCalibration(0);
    addLog("SGTHRS=" + String(thrs) + " TPWMTHRS=" + String(tpwmCandidate)
           + " (" + String(12000000.0f / tpwmCandidate / 3200.0f * 60.0f, 0) + " RPM)");
}

// --- SPEED TEST (adaptiv) ---
void runSpeedTest(int i) {
    if (i != 0 || !sys.cal[0].valid) { addLog("Error: Calib M0 first!"); return; }
    if (!sys.m[0].enabled) setMotorPower(0, true);

    uint16_t cur = activeCurrent(0);
    driverX.rms_current(cur);
    uint8_t thrs = sys.cal[0].sgThrs > 0 ? sys.cal[0].sgThrs : MOTOR_SGTHRS_DEFAULT;
    driverX.SGTHRS(thrs);

    float rpm = 300.0f, lastGood = rpm;
    bool failed = false, currentBoosted = false;
    clearTelemetry();
    addLog("Parcour M0 (" + String(cur) + "mA SGTHRS=" + String(thrs) + ")");

    while (rpm <= 2500.0f && !failed) {
        addLog("Try " + String(rpm, 0) + " RPM");
        steppers[0]->setMaxSpeed(rpmToSps(rpm));
        steppers[0]->setAcceleration(rpmToSps(rpm) * 4);
        steppers[0]->move(6400);
        while (steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            recordTelemetry("SPEED", rpm);
            yield();
        }

        setSpreadCycle(0, true);
        steppers[0]->setSpeed(200);
        unsigned long sS = millis(); bool found = false;
        while (millis() - sS < 5000) {
            if (digitalRead(TACHO_PIN) == LOW) { found = true; break; }
            steppers[0]->runSpeed();
            recordTelemetry("DRIFT", rpm);
            yield();
        }
        long drift = abs(steppers[0]->currentPosition() % 3200);
        setSpreadCycle(0, false);

        if (!found || drift > 60) {
            // Strom erhöhen und nochmal versuchen
            if (cur + 100 <= MOTOR_CURRENT_MAX_MA) {
                cur += 100;
                driverX.rms_current(cur);
                sys.cal[0].learnedCurrentMA = cur;
                currentBoosted = true;
                sys.cal[0].stableRuns = 0;
                addLog("Drift-Fail -> Strom " + String(cur) + "mA, Retry");
                // rpm nicht erhöhen → gleiche Geschwindigkeit nochmal
            } else {
                failed = true;
                addLog("FAIL " + String(rpm, 0) + " RPM (max Strom erreicht)");
            }
        } else {
            lastGood = rpm;
            rpm += 100.0f;
        }
    }

    // Feintuning nach Grobfehler (nur wenn aktiviert)
    if (failed && !sys.parcour.doFine) {
        addLog("Fine-Tuning deaktiviert.");
        failed = false; // nicht als Gesamtfehler werten
    }
    if (failed) {
        rpm = lastGood + 10.0f; failed = false;
        addLog("Fine-tuning...");
        while (rpm < (lastGood + 100.0f) && !failed) {
            steppers[0]->setMaxSpeed(rpmToSps(rpm));
            steppers[0]->move(3200);
            while (steppers[0]->distanceToGo() != 0) {
                steppers[0]->run();
                recordTelemetry("FINE", rpm);
                yield();
            }
            setSpreadCycle(0, true); steppers[0]->setSpeed(200);
            waitForSensor(0, LOW, 5000);
            if (abs(steppers[0]->currentPosition() % 3200) > 60) failed = true;
            else { lastGood = rpm; rpm += 10.0f; }
            setSpreadCycle(0, false);
        }
    }

    // Lerngedächtnis: stabile Läufe zählen, Strom ggf. reduzieren
    if (!currentBoosted) {
        if (sys.cal[0].stableRuns < 255) sys.cal[0].stableRuns++;
        if (sys.cal[0].stableRuns >= 3) {
            uint16_t reduced = cur - 50;
            if (reduced >= MOTOR_CURRENT_MIN_MA) {
                cur = reduced;
                sys.cal[0].learnedCurrentMA = cur;
                addLog("Stabil x" + String(sys.cal[0].stableRuns) + " -> Strom ->" + String(cur) + "mA");
            }
        }
    }

    sys.cal[0].maxRpm = lastGood;
    sys.cal[0].learnedCurrentMA = cur;
    saveCalibration(0);
    addLog("RESULT: " + String(lastGood, 0) + " RPM | " + String(cur) + "mA | SGTHRS=" + String(thrs));
}

// --- INERTIA TEST ---
void runInertiaTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);
    driverX.rms_current(activeCurrent(0));
    addLog("Inertia M0...");
    float accel = 1000.0f, lastGood = accel;
    bool failed = false;
    float testSpd = sys.cal[0].maxRpm > 0 ? sys.cal[0].maxRpm * 0.7f : 600.0f;

    while (accel <= 40000.0f && !failed) {
        addLog("Try Accel " + String(accel, 0));
        steppers[0]->setMaxSpeed(rpmToSps(testSpd));
        steppers[0]->setAcceleration(accel);
        long base = steppers[0]->currentPosition();
        steppers[0]->moveTo(base + 3200);
        while (steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }
        steppers[0]->moveTo(base - 3200);
        while (steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }
        steppers[0]->moveTo(base);
        while (steppers[0]->distanceToGo() != 0) { steppers[0]->run(); recordTelemetry("ACCEL", accel); yield(); }

        setSpreadCycle(0, true); steppers[0]->setSpeed(200);
        waitForSensor(0, LOW, 5000);
        if (abs(steppers[0]->currentPosition() % 3200) > 60) failed = true;
        else { lastGood = accel; accel += 2000.0f; }
        setSpreadCycle(0, false);
    }
    sys.cal[0].maxAccel = lastGood;
    saveCalibration(0);
    addLog("RESULT: " + String(lastGood, 0) + " Accel");
}

// --- COAST TEST ---
// Misst Ausrollverhalten: kurze Beschleunigungsrampe (accelSteps),
// dann Freilauf bei konstantem Tempo (coastSteps), danach Sensor-Drift-Check.
// Wiederholt COAST_CYCLES mal mit steigender Testgeschwindigkeit.
#define COAST_CYCLES     5
#define COAST_ACCEL_STEPS 200
#define COAST_COAST_STEPS 400

void runCoastTest(int i) {
    if (i != 0 || !sys.cal[0].valid) return;
    if (!sys.m[0].enabled) setMotorPower(0, true);

    float baseRpm = sys.cal[0].maxRpm > 0
                    ? min(sys.cal[0].maxRpm * 0.5f, 400.0f)
                    : 200.0f;
    addLog("Coast M0 Basis=" + String(baseRpm, 0) + " RPM");

    for (int cycle = 0; cycle < COAST_CYCLES; cycle++) {
        float testRpm = baseRpm + cycle * (baseRpm / COAST_CYCLES);
        float testSps = rpmToSps(testRpm);

        // --- Beschleunigungsrampe (kurz & steil) ---
        steppers[0]->setMaxSpeed(testSps);
        steppers[0]->setAcceleration(testSps * 8); // steile Rampe
        steppers[0]->move(COAST_ACCEL_STEPS);
        while (steppers[0]->distanceToGo() != 0) {
            steppers[0]->run();
            recordTelemetry("C_ACCEL", testRpm);
            yield();
        }

        // --- Freilauf: konstante Geschwindigkeit, keine Verzögerungsrampe ---
        long posStart = steppers[0]->currentPosition();
        steppers[0]->setSpeed(testSps);
        for (int s = 0; s < COAST_COAST_STEPS; s++) {
            steppers[0]->runSpeed();
            if (s % 20 == 0) recordTelemetry("COAST", testRpm);
            yield();
        }
        long posEnd = steppers[0]->currentPosition();

        // --- Sensor-Drift-Check ---
        setSpreadCycle(0, true);
        steppers[0]->setSpeed(200);
        bool found = waitForSensor(0, LOW, 5000);
        long sensorPos = steppers[0]->currentPosition() % 3200;
        long drift = abs(posEnd - posStart - COAST_COAST_STEPS);
        setSpreadCycle(0, false);

        recordTelemetry("C_CHECK", testRpm);
        addLog("Coast #" + String(cycle+1)
               + " @" + String(testRpm, 0) + "RPM"
               + " drift=" + String(drift) + "st"
               + " sensor=" + String(found ? String(sensorPos) : "miss"));
    }

    addLog("Coast abgeschlossen.");
}

// --- UPDATE LOOP ---
void updateMotors() {
    if (sys.pendingStop) { steppers[0]->stop(); sys.pendingStop = false; }
    if (sys.m[0].enabled) steppers[0]->run();
}
