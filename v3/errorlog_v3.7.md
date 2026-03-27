# ERRORLOG — PerlinNoise Firmware v3.7.xx
**Analysiert:** 2026-03-27
**Aktuelle Version:** 3.7.13 (firmware_v3_3.7.13_20260327_181255.bin)
**Scope:** Quellcode-Review `/perlinnoise/v3/src/` auf Bugs, Schwächen, Optimierungen

---

## CHANGELOG (Fixes durch Code-Review)

| Datum | Version | Fix | Datei |
|-------|---------|-----|-------|
| 2026-03-27 | 3.7.13 | `tCache.stall`: `(ds & 0x1)` → `(tCache.sg == 0)` — korrekter TMC2209-Stall-Indikator | `Telemetry.cpp:60` |
| 2026-03-27 | 3.7.13 | `/telemetry`-Handler: 52KB-String-Copy aus `portENTER_CRITICAL` herausgezogen | `main_v3.cpp:281` |
| (vor Review) | 3.7.x | `clearTelemetry()` + `recordTelemetry()` mit `portENTER_CRITICAL` geschützt | `Telemetry.cpp` |
| (vor Review) | 3.7.x | Parcour: `pendingStop`-Checks zwischen Test-Schritten | `main_v3.cpp` |

---

## ZUSAMMENFASSUNG

| Kategorie | Anzahl |
|-----------|--------|
| CRITICAL  | 1      |
| WARNING   | 12     |
| INFO/OPT  | 7      |
| **Gesamt**| **20** |

---

## CRITICAL

### [C1] WiFi-Passwort im Klartext — `wifi_settings.h:6`
**Severity:** CRITICAL
**Problem:** WiFi-Credentials hardcoded im Source-File:
```c
#define DEFAULT_PASS "KommInDieGaenge!"
```
Passwort ist im Binary und im Repository im Klartext sichtbar. Bei Veröffentlichung des Repos sofort kompromittiert.
**Fix:** Credentials aus Source entfernen → NVS/EEPROM mit Encryption oder Build-Environment-Variable. Datei in `.gitignore` aufnehmen.

---

## WARNINGS

### [W1] ~~Race Condition — `telemCSV` ohne Mutex~~ — BEHOBEN
`clearTelemetry()` und `recordTelemetry()` sind korrekt mit `portENTER_CRITICAL` geschützt. `/telemetry`-Handler kopiert String vor Critical Section (Fix 2026-03-27).

### [W1-detail] Race Condition — `telemCSV` ohne Mutex — `Telemetry.cpp:16,21,40`
**Severity:** WARNING
**Problem:** `telemCSV` (globaler String) wird von Core 0 (WiFi/Web-Handler) gelesen und von Core 1 (Motor-Task) ohne Synchronisation beschrieben. `clearTelemetry()` und `recordTelemetry()` können gleichzeitig mit dem `/telemetry`-Handler laufen → Heap-Korruption oder Crash bei String-Reallozierung.
**Fix:**
```c
void recordTelemetry(const char* phase, float val) {
    portENTER_CRITICAL(&motorMux);
    telemCSV += line;
    portEXIT_CRITICAL(&motorMux);
}
```

### [W2] Division durch Null / Integer Overflow — `Sensor.cpp:57`
**Severity:** WARNING
**Problem:**
```c
return (uint16_t)min(9999UL, 60000UL / period);
```
Kein Check ob `period > 0`. Bei `period < 6` läuft `60000 / period` außerhalb uint16_t-Range.
**Fix:**
```c
if (period == 0 || period < 6) return 0;
return (uint16_t)min(9999UL, 60000UL / period);
```

### [W3] Emergency Stop geht verloren — `MotorControl.cpp:243`
**Severity:** WARNING
**Problem:** `sys.pendingStop` wird nur in `updateMotors()` geprüft, das am Ende der Core1-Loop-Iteration sitzt. Während `touchEdge()`, `waitForSensorStable()` oder anderen blocking-Loops läuft, kommt kein Stop durch.
**Fix:** `pendingStop`-Check in alle `while(stepper->isRunning())` Schleifen einbauen:
```c
while (stepper->isRunning()) {
    if (sys.pendingStop) { stepper->stopMove(); return -1; }
    yield();
}
```

### [W4] Blocking Loop ohne Timeout — `MotorControl.cpp:45-66` (touchEdge)
**Severity:** WARNING
**Problem:** `while(stepper->isRunning()) yield()` und `waitForSensorStable()` haben kein Timeout auf dem inneren isRunning-Loop. Bleibt Stepper hängen, kehrt die Funktion nie zurück.
**Fix:**
```c
unsigned long t = millis();
while (stepper->isRunning()) {
    if (millis() - t > 10000) { addLog("Err: stepper timeout"); return -1; }
    yield();
}
```

### [W5] Microstep-Skalierung Race Condition — `Driver.cpp:46-59`
**Severity:** WARNING
**Problem:** `stepsPerRev` wird in der Critical Section aktualisiert, aber `driverX.microsteps(ms)` (UART) passiert danach außerhalb. Zwischen beiden liegt ein Fenster, in dem `stepsPerRev` und TMC2209-Register inkonsistent sind. Bewegungsbefehle in diesem Fenster nutzen falsche Steps.
**Fix:** UART-Schreibvorgang muss abschließen, **bevor** `stepsPerRev` und `currentMicrosteps` gesetzt werden.

### [W6] Stepper NULL — `getPositionDeg()` gibt 0 zurück — `Driver.cpp:85`
**Severity:** WARNING
**Problem:**
```c
float getPositionDeg() { return stepper ? stepsToDeg(...) : 0.0f; }
```
0.0f ist nicht von Position 0° unterscheidbar. Caller kann Initialisierungsfehler nicht erkennen.
**Fix:** `return NAN;` bei `stepper == NULL`.

### [W7] Motor-Index nicht vollständig validiert — `Driver.cpp:61-78`
**Severity:** WARNING
**Problem:** `setMotorPower(int i, ...)` prüft nur `if (i != 0) return`, aber `sys.m[i]`-Array hat 4 Einträge. In `loadCalibration()` wird in Schleife über 4 Motoren iteriert — 3 davon sind funktional leer. Kann bei unerwarteten Callern zu Out-of-Bounds führen.
**Fix:**
```c
#define MOTOR_COUNT 1
if (i < 0 || i >= MOTOR_COUNT) return;
```

### [W8] ~~Log-Buffer: teures substring()~~ — BEHOBEN (vor Review)
Trim bei 6000→3000 bereits implementiert.

### [W8-detail] Log-Buffer: teures substring() — `MotorControl.cpp:9-14`
**Severity:** WARNING
**Problem:**
```c
sys.log += msg + "\n";
if (sys.log.length() > 8000) sys.log = sys.log.substring(sys.log.length() - 4000);
```
`substring()` auf einem 8000-Byte-String ist auf dem ESP32 teuer (Heap-Reallozierung). Trigger erst bei 8000 Bytes, kann also kurz darüber schießen.
**Fix:** Früher trimmen (z.B. bei 6000 → auf 3000), `sys.log.reserve(8000)` einmalig.

### [W9] opState Index ohne Bounds-Check — `main_v3.cpp:245`
**Severity:** WARNING
**Problem:**
```c
const char* opNames[] = {"idle","homing","calib","learn","test","show"};
String(opNames[sys.opState])  // kein Bounds-Check
```
Bei korruptem `sys.opState` > 5: undefined behavior / Crash.
**Fix:**
```c
if (sys.opState >= 6) sys.opState = 0;
```

### [W10] Sensor-Polling-Delay zu kurz bei EMI — `Sensor.cpp:36-44`
**Severity:** WARNING
**Problem:** `checkSensorStable()` wartet je 50µs zwischen Reads. BUG_REPORT nennt EMI durch TMC2209-Switching (Noise ~10-20µs Periode). 5-Hit-Filter kann bei periodischem Noise in Resonanz geraten.
**Fix:** Delay auf 100µs erhöhen oder delay adaptiv aus Motorgeschwindigkeit ableiten.

### [W11] UART-Timeout nicht konfigurierbar — `Driver.cpp:32,55,66,73,108`
**Severity:** WARNING
**Problem:** Alle `xSemaphoreTake(uartMutex, pdMS_TO_TICKS(50))` verwenden Hard-coded 50ms. `applyDriverSettings()` macht ~8 UART-Writes → bis zu 400ms Blockade. Kein Retry.
**Fix:**
```c
#define UART_TIMEOUT_MS 50
// + Retry-Logik mit max 3 Versuchen
```

### [W12] Kalibrierungsdaten ohne Plausibilitätsprüfung — `MotorControl.cpp:24-36`
**Severity:** WARNING
**Problem:** `loadCalibration()` prüft nur `nvsVersion == 3619`, keine Range-Checks auf `triggerStartDeg` / `triggerEndDeg`. Korrupte NVS-Daten können extreme Schrittzahlen in `degToSteps()` erzeugen.
**Fix:**
```c
if (sys.cal[i].triggerStartDeg > 0 || sys.cal[i].triggerEndDeg < 0
    || fabsf(sys.cal[i].triggerEndDeg - sys.cal[i].triggerStartDeg) > 10.0f) {
    sys.cal[i].valid = false;
}
```

---

## INFO / OPTIMIERUNGEN

### [O1] String-Operationen im Web-Handler — `main_v3.cpp:231-251`
**Severity:** INFO
**Problem:** Bei 350ms-Polling: 3× globale `replace()` auf bis zu 4000-Byte-String + mehrere `String +=` pro Request. Hohe Heap-Fragmentierung.
**Fix:** `snprintf` auf char-Array statt String-Konkatenation.

### [O2] TelemCache läuft auch im Idle — `main_v3.cpp:295-296`
**Severity:** INFO
**Problem:** Task alle 300ms, liest ~8 TMC2209-UART-Register — auch wenn Motor nicht läuft.
**Fix:**
```c
if (sys.opState != OP_IDLE) updateTelemCache();
```

### [O3] Redundante Division in RPM-Konvertierung — `Driver.cpp:23-28`
**Severity:** INFO
**Problem:** `stepsPerRev / 60.0f` wird in `rpmToSps()`, `spsToRpm()`, `rpmToTpwmthrs()` separat berechnet.
**Fix:** Pre-computed Konstante `SPS_PER_RPM = stepsPerRev / 60.0f`.

### [O4] TPWMTHRS hardcoded auf 100 RPM — `Driver.cpp:38`
**Severity:** INFO
**Problem:** SpreadCycle-Schwelle bei 100 RPM. PROJEKT_DOKU sagt SG nur >~600 RPM sinnvoll. Threshold zu niedrig für SG-basierte Lasttests.
**Fix:** `#define SPREADCYCLE_RPM_MIN 200` (mindestens, besser 300).

### [O5] Test-Funktionen sind Stubs — `MotorControl.cpp:216-240`
**Severity:** INFO
**Problem:** `runInertiaTest`, `runCoastTest`, `runKatapult`, `runFreqSweep`, `runPerformanceShow` → alle loggen Startmeldung, machen nichts. UI zeigt keinen Fehler.
**Fix:** `addLog("Err: <Name> not implemented");` und früh return, solange nicht fertig.

### [O6] PCNT Magic Number ohne Kommentar — `CalibTest.cpp:21`
**Severity:** INFO
**Problem:** `& 0xFFFF` mit Sign-Extension ist korrekt für 16-bit PCNT, aber nicht kommentiert.
**Fix:** Inline-Kommentar einfügen.

### [O7] `currentMicrosteps` initialisiert, Stepper aber NULL — `Driver.cpp:19`
**Severity:** INFO
**Problem:** Startwert 64 / 12800 auch wenn `initDriver()` scheitert. Folgefunktionen rechnen mit invaliden Werten.
**Fix:** Auf 0 initialisieren, erst nach erfolgreichem `initDriver()` setzen.

---

## PRIORITÄTEN

| Prio | Maßnahme |
|------|----------|
| **Sofort** | C1: WiFi-Passwort aus Source entfernen |
| **Sofort** | W1: telemCSV mit Mutex schützen |
| **Sofort** | W2: Division-by-Zero in getTachoRpm() |
| **Hoch**   | W3: pendingStop in alle Blocking-Loops |
| **Hoch**   | W4: Timeout in isRunning()-Loops |
| **Hoch**   | W5: Microstep-Race-Condition fixen |
| **Mittel** | W9, W12: Bounds-Checks Calibration + opState |
| **Niedrig**| O1–O7: Optimierungen |

---

*Generiert von Claude Code (claude-sonnet-4-6) am 2026-03-27*
