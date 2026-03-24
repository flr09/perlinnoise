# Laufanalyse – Motor X (36BYG1204-A-6QHT)

**Ziel:** Vergleich von Firmware-Ständen, Telemetrie-Antworten und Motorverhalten, um die Ursache des Jitterns zu ergründen.

---

## Beobachtetes Verhalten

### Symptom: Motor "jittert"

Der Motor läuft **unruhig** — sichtbares Zittern bei niedrigen Drehzahlen, kein gleichmäßiger Lauf. Das Jittern tritt besonders auf bei:

- Langsamer Kriechfahrt (200 sps, Drift-Check)
- Beim Anlauf aus dem Stillstand
- Unterhalb ca. 300 RPM allgemein

**Mögliche Ursachen (noch nicht eindeutig eingegrenzt):**

| Hypothese | Indiz aus Telemetrie | Status |
|-----------|---------------------|--------|
| Zu wenig Strom für Drehmomenbedarf | cs_actual adaptiv von 18 → 26, Jitter bleibt | **offen** |
| StealthChop-Resonanz im Niedrigdrehzahlbereich | SG_RESULT 0–6 bei 200 sps | **wahrscheinlich** |
| Mechanische Last / Reibung | Drift-Check schlägt bei 200 sps fehl (Sensor nicht gefunden) | **möglich** |
| TPWMTHRS-Schwelle falsch → falscher Modus bei Testdrehzahl | noch nicht vermessen nach TPWMTHRS-Lernlauf | **offen** |
| Pancake-Bauform: geringes Trägheitsmoment → empfindlich für Schrittverlust | Drift >60 Steps nach 2 Umdrehungen bei 300 RPM | **wahrscheinlich** |

---

## Firmware-Vergleich

### v3.2.0 — Baseline

**Änderungen:** CALIB-Fix (360°), POWER-Fix (Re-Init), Telemetrie eingebaut
**Parcour-Bereich:** 300–2500 RPM
**Strom:** 600 mA fest
**Modus:** kein TPWMTHRS, kein SGTHRS, kein IHOLD

| Lauf | Max RPM | cs_actual | SG-Range | Stall-Events | Laufdauer |
|------|---------|-----------|----------|-------------|-----------|
| parcour_183755.csv | **440 RPM** | 18 (fest) | 2–46 | 19 | 102 s |

**Interpretation:**
- Motor erreicht 440 RPM, scheitert dann am Drift-Check
- SG_RESULT generell sehr niedrig (2–46) → hohe Last oder ungeeigneter Modus
- cs_actual=18 durch alle Phasen → kein adaptiver Strom
- 19 Stall-Events, alle durch `sg==0` Schwelle (Default, keine Bedeutung)

---

### v3.3.0 — Adaptives Tuning

**Änderungen:** IHOLD/IRUN getrennt, TPWMTHRS (auto StealthChop↔SpreadCycle), SGTHRS-Lernlauf, adaptiver Strom (±50–100 mA je nach Drift-Ergebnis), Parcour 1000–10000 RPM

| Lauf | Max RPM | cs_actual | SG-Range | Stall-Events | Laufdauer |
|------|---------|-----------|----------|-------------|-----------|
| parcour_151061.csv | **310 RPM** | 20–26 | 0–16 | **211** | 98 s |
| parcour_393983.csv | **300 RPM** | 26 | 0–6 | 34 | 13 s |

**Interpretation:**
- Adaptiver Strom wirkt: cs_actual steigt von 20 auf 26 über Läufe hinweg
- SG_RESULT noch tiefer als v3.2.0 (0–16 statt 2–46) → TPWMTHRS schaltet möglicherweise in falschen Modus
- 211 Stall-Events: SGTHRS wurde durch Lernlauf auf ~10 gesetzt (60 % von SG-Mean≈16) → fast jeder Sample triggert Stall-Flag im CSV, obwohl Motor läuft
- Max RPM **gesunken** trotz höherem Strom → deutet auf Modus-Problem, nicht Strom-Problem

---

## Kritische Beobachtung: SG-Werte

```
v3.2.0: SG_RESULT  2 – 46  (SPEED-Phase, 300 RPM)
v3.3.0: SG_RESULT  0 – 16  (SPEED-Phase, 300 RPM, nach TPWMTHRS-Lernlauf)
```

**Der Rückgang nach v3.3.0 ist verdächtig.** Mögliche Erklärungen:

1. **TPWMTHRS zu hoch gesetzt** → Motor läuft bei Testdrehzahl (300–440 RPM) noch in StealthChop statt SpreadCycle. In StealthChop liefert SG_RESULT andere (niedrigere) Werte.
2. **SGTHRS-Lernlauf im falschen Modus gemessen** → `learnSGProfile()` läuft ohne SpreadCycle-Override, misst SG im StealthChop-Bereich, setzt SGTHRS zu niedrig.
3. **Motor hat intrinsisch niedrige SG-Werte** → 36BYG1204 Pancake mit 1,29 mH ist kein idealer StallGuard-Motor (für SG braucht man höhere Induktivität).

---

## Root-Cause Analyse: „Motor dreht 50 RPM, Log sagt Try 3200 RPM"

### Bug 1 – PARCOUR_RPM_START zu hoch (open-loop Schrittgenerator)

**Symptom:** Log zeigt „Try 3200 RPM", Motor dreht sichtbar mit ~50 RPM.

**Ursache:** AccelStepper ist ein offener Regelkreis. `setMaxSpeed(rpmToSps(3200))` = 170667 sps wird befohlen, der Motor kann physisch max ~440 RPM = 23467 sps. Der Schrittgenerator zählt die *befohlenen* Schritte — der Motor dreht so schnell er kann, verliert Schritte und bleibt irgendwo stehen. Der Schrittzähler ist danach bedeutungslos.

```
PARCOUR_RPM_START = 1000  → rpmToSps(1000) = 53333 sps
Motor max physisch:          ~440 RPM        = 23467 sps
→ Motor läuft bei ~440 RPM, verliert Schritte, Zähler zeigt 53333 sps
→ Nächste Iteration: 1100 RPM (da kein Fehler erkannt)
→ Nach ~20 Iterationen: Try 3200 RPM
```

### Bug 2 – Drift-Check pos%3200 lieferte immer 0

**Symptom:** Motor verliert 80% der Schritte, Test meldet trotzdem Erfolg.

**Ursache:** Drift-Check war `abs(currentPosition() % 3200)`. Bei `move(6400)` (= 2 Umdrehungen):
```
6400 % 3200 = 0  → drift = 0 → immer als OK gewertet
```
Die Prüfung hat niemals einen echten Schrittausfall erkannt.

### Bug 3 – learnSGProfile maß in StealthChop (falsche SG-Werte)

**Symptom:** SG_RESULT nach v3.3.0 schlechter als v3.2.0 (0–16 statt 2–46).

**Ursache:** Messpunkte waren 200–4000 sps = **3,75–75 RPM**. TPWMTHRS war auf 200 RPM gesetzt → alle Messpunkte lagen in StealthChop. Im StealthChop-Modus liefert SG_RESULT inhärent niedrige Werte (motor-internal compensation, kein echter Laststrom). SGTHRS wurde als 60% von ~10 = **6** gelernt → fast jeder Sample triggerte Stall-Flag.

```
v3.2.0: kein TPWMTHRS → alles SpreadCycle → SG 2-46
v3.3.0: TPWMTHRS@200RPM, Messung bei 3-75RPM → alles StealthChop → SG 0-16
```

---

## v3.4.0 — Fixes

**Datum:** 2026-03-24
**Änderungen:**

| Fix | Vorher | Nachher |
|-----|--------|---------|
| RPM-Start | 1000 RPM fest | dynamisch: 80% von cal.maxRpm, min 200 RPM |
| RPM-Validierung | pos%3200 (immer 0) | Tacho-Pulse zählen (measureActualRpm) |
| Modus | Auto StealthChop↔SpreadCycle | SpreadCycle explizit erzwungen |
| SG-Messung | 200-4000 sps (3-75 RPM, StealthChop) | 100-400 RPM, SpreadCycle explizit |
| TPWMTHRS | 200 RPM Grenze | 100 RPM (StealthChop nur bei Stillstand) |
| TelemCache | UART direkt in Schrittschleife | Cache getrennt, alle 300ms refresht |

**`measureActualRpm(cmdSps, windowMs)`:**
- Fährt Motor bei `cmdSps` via `runSpeed()` (konstante Geschwindigkeit)
- Zählt HIGH→LOW Flanken am TACHO_PIN über `windowMs` (= eine Umdrehung pro Puls)
- `actualRpm = revCount * 60000 / windowMs`
- Validierung: `ok = actualRpm >= cmdRpm * 0.65f`

**Erwartetes Verhalten nach v3.4.0:**
- Erster Lauf (keine Kalibration): Start bei 200 RPM, schrittweise hoch bis Motor versagt
- Folgender Lauf: Start bei 80% des gelernten Max → schnellerer Parcour
- Log zeigt: `Ist=440 Soll=440 (100%)` statt `Try 3200 RPM`

---

## Bug-Report v3.3.x — ISR + Dual-Core Umbau (aktueller Codestand)

> **Kontext für den nächsten Entwickler:**
> Die Idee war gut: Tacho-Pulse per ISR zählen (kein Polling), UART-Reads (SG, CS) auf Core 0 auslagern damit Core 1 den Schrittgenerator störungsfrei betreiben kann. Die Architektur stimmt. In der Umsetzung sind aber mehrere Fehler entstanden, die dazu führen dass im Grunde gar nichts mehr korrekt funktioniert.

### Architektur-Intent (korrekt)

```
Core 0  (WiFi/Web)   → updateTelemCache() alle 200ms  (UART, langsam, unkritisch)
Core 1  (MotorTask)  → AccelStepper run(), Tests       (zeitkritisch, kein UART)
ISR     (Core 1)     → pulseCount++  bei FALLING       (sofort, kein AccelStepper-Aufruf)
```

### Was davon wirklich gebaut wurde

```
Core 0  (WiFi/Web)   → updateTelemCache() ← WIRD NIE AUFGERUFEN   ← BUG X1
Core 1  (MotorTask)  → AccelStepper run(), Tests                   ✓
ISR     (Core 1)     → pulseCount++                                ✓ (fast)
```

---

### 🔴 X1 — `updateTelemCache()` wird nirgends aufgerufen → Telemetrie immer Null

**Problem:** `tCache` (sg, cs, stall, ola, olb) wird nie aktualisiert. Alle Telemetrie-Spalten zeigen 0. Der ganze TelemCache-Ansatz ist richtig konzipiert, aber nie verdrahtet.

**Fix:** In `setup()` einen Task auf Core 0 anlegen:
```cpp
xTaskCreatePinnedToCore([](void*){
    for(;;) { updateTelemCache(); vTaskDelay(pdMS_TO_TICKS(200)); }
}, "TelemCache", 2048, NULL, 1, NULL, 0); // Core 0!
```
Das ist die eine Zeile die fehlt. Dadurch laufen UART-Reads auf Core 0 und blockieren nie den Schrittgenerator auf Core 1.

---

### 🔴 X2 — `measureActualRpm()`: Pulse werden während der Einlaufphase gezählt

**Problem:**
```cpp
float measureActualRpm(float cmdSps, uint32_t windowMs) {
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux);
    steppers[0]->setSpeed(cmdSps);
    // ← kein Warten! Motor läuft erst an
    unsigned long start = millis();
    while(millis() - start < windowMs) { steppers[0]->runSpeed(); yield(); }
    // Pulses aus Anlaufphase + Messphase gemischt → RPM zu niedrig
```
`runSpeed()` startet sofort bei `cmdSps` — aber der Motor kann nicht schlagartig beschleunigen. In den ersten ~300–500ms ist die echte Drehzahl deutlich unter `cmdSps`. Diese Pulse werden mitgezählt. Ergebnis: `measureActualRpm` liefert bei jedem Aufruf zu niedrige Werte → `actualRpm < rpm * 0.7f` → sofort FAIL → Parcour bricht nach dem ersten Schritt ab.

**Fix:** Einlaufzeit vor dem Pulse-Reset:
```cpp
float measureActualRpm(float cmdSps, uint32_t windowMs) {
    steppers[0]->setSpeed(cmdSps);
    unsigned long settle = millis();
    while(millis() - settle < 400) { steppers[0]->runSpeed(); yield(); } // einlaufen
    portENTER_CRITICAL(&motorMux); pulseCount = 0; portEXIT_CRITICAL(&motorMux); // DANN nullen
    unsigned long start = millis();
    while(millis() - start < windowMs) { steppers[0]->runSpeed(); yield(); }
    ...
```

---

### 🔴 X3 — `learnSGProfile()`: SG-Samples während Beschleunigung/Verzögerung

**Problem:**
```cpp
steppers[0]->setMaxSpeed(sps); steppers[0]->setAcceleration(sps*4);
steppers[0]->move(6400);
while(steppers[0]->distanceToGo() != 0) {
    steppers[0]->run();
    if(abs(steppers[0]->speed()) > sps*0.8f) { // soll "nur bei Vollspeed" samplen
        sgSum += driverX.SG_RESULT();
```
Mit `Acceleration = sps*4` erreicht der Motor bei `move(6400)` die Vollgeschwindigkeit nach etwa 800 Schritten — und bremst ab Schritt 5600 wieder ab. Bei 6400 Schritten gibt es nur ca. 4800 Schritte Konstantfahrt. Die Bedingung `speed() > sps*0.8f` hilft, aber `speed()` ist AccelSteppers berechneter Soll-Takt, nicht die echte Motordrehzahl. UART-Reads (`driverX.SG_RESULT()`) mitten in der Beschleunigungsphase liefern instabile Werte.

**Fix:** `runSpeed()` statt `move()` für die SG-Messung, mit Einlaufzeit:
```cpp
steppers[0]->setSpeed(sps);
unsigned long settle = millis();
while(millis() - settle < 800) { steppers[0]->runSpeed(); yield(); } // einlaufen
// dann 1.5s lang SG samplen
```

---

### 🔴 X4 — `setMotorPower()` ruft `driverX.begin()` bei jedem Aufruf → Register-Reset

**Problem:**
```cpp
void setMotorPower(int i, bool on) {
    if (on) {
        driverX.begin(); // ← setzt ALLE TMC2209-Register zurück!
        driverX.toff(5);
        applyDriverSettings(...); // re-setzt sie dann wieder
```
`homeMotor`, `characterizeSensor`, `learnSGProfile`, `runSpeedTest`, `runCoastTest`, `runInertiaTest` rufen alle `setMotorPower(0, true)` am Anfang auf. Das bedeutet: der TMC2209 wird vor jeder Funktion komplett zurückgesetzt und dann neu konfiguriert. Das kostet ~20ms UART-Writes und verursacht kurzen Momenten ohne Strom/Chopper.

**Fix:** `driverX.begin()` nur einmal in `initMotors()`. In `setMotorPower` nur ENABLE_PIN und toff steuern:
```cpp
void setMotorPower(int i, bool on) {
    if (i != 0) return;
    sys.m[i].enabled = on;
    if (on) {
        driverX.toff(5);          // Chopper an
        digitalWrite(ENABLE_PIN, LOW);
    } else {
        driverX.toff(0);          // Chopper aus
        digitalWrite(ENABLE_PIN, HIGH);
    }
}
// initMotors() ruft einmalig driverX.begin() + applyDriverSettings()
```

---

### 🔴 X5 — `/telemetry` fehlt CORS-Header → `telemetry_viewer.html` funktioniert nicht

**Problem:**
```cpp
server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest *r){
    AsyncWebServerResponse *res = r->beginResponse(200, "text/csv", telemCSV);
    res->addHeader("Content-Disposition", "attachment; filename=\"parcour.csv\"");
    // FEHLT: res->addHeader("Access-Control-Allow-Origin", "*");
    r->send(res);
});
```
Der `telemetry_viewer.html` ist eine lokale HTML-Datei die per `fetch()` auf `http://perlin-v3.local/telemetry` zugreift. Browser blocken Cross-Origin-Anfragen ohne CORS-Header → "Fetch failed".

**Fix:** Eine Zeile ergänzen:
```cpp
res->addHeader("Access-Control-Allow-Origin", "*");
```

---

### 🟡 X6 — CSV-Format geändert → `telemetry_viewer.html` zeigt leere Spalten

**Problem:** Aktuelles CSV hat 10 Spalten:
```
ts_ms,phase,val,pos_steps,spd_sps,sg_result,cs_actual,stall,ola,olb
```
Der `telemetry_viewer.html` erwartet 14 Spalten (inkl. `cur_a`, `cur_b`, `otpw`, `ot`). Fehlen diese, zeigen die entsprechenden Charts leer. Außerdem hat `TelemCache` diese Felder gar nicht mehr.

**Fix:** Entweder `TelemCache` um `cur_a`, `cur_b`, `otpw`, `ot` ergänzen und CSV zurück auf 14 Spalten bringen, oder `telemetry_viewer.html` an das neue 10-Spalten-Format anpassen.

---

### 🟡 X7 — Versionsstring inkonsistent (HTML ≠ FW_VERSION)

```
MotorControl.h:  FW_VERSION "3.3.6"
main_v3.cpp:     "v3.3.9 (Robust Fix)"  ← hardcoded im HTML
                 "v3.3.7 (ISR + High-Speed Merged)"  ← im Log-Div
```
Drei verschiedene Versionsstrings. Bei Debugging unklar welche Version wirklich geflasht ist.

**Fix:** Nur `FW_VERSION` aus dem Header verwenden, im HTML per JS setzen wie in den früheren Versionen:
```html
<span id="fwVer">V3 ...</span>
```
```js
if(s.fw) document.getElementById('fwVer').innerText = 'V3 ' + s.fw;
```
Und `/status` muss `"fw":"` + `FW_VERSION` + `"` zurückgeben.

---

### 🟡 X8 — `/cmd?a=pwr` ruft `setMotorPower` direkt auf Core 0 (ohne Mutex)

**Problem:** Der WebServer läuft auf Core 0. `setMotorPower` greift auf `sys.m[0].enabled`, `driverX`, und `ENABLE_PIN` zu — alles was auch Core 1 (TaskCore1 → `updateMotors`) gleichzeitig liest/schreibt. Kein Mutex schützt diesen Zugriff.

In der Praxis selten ein Problem (Button-Drücke sind selten), aber technisch eine Daten-Race.

**Fix:** Power-Befehle über `pendingPower`-Flag in SystemState, nicht direkt:
```cpp
// Core 0 (WebServer):
else if(a=="pwr") sys.pendingPower = !sys.m[m].enabled;
// Core 1 (TaskCore1):
if (sys.pendingPower >= 0) { setMotorPower(0, sys.pendingPower); sys.pendingPower = -1; }
```

---

### Zusammenfassung der neuen Bugs

| # | Schwere | Problem | Auswirkung | Fix-Aufwand |
|---|---------|---------|------------|-------------|
| X1 | 🔴 | `updateTelemCache()` nie aufgerufen | Telemetrie immer 0 | 1 Zeile in setup() |
| X2 | 🔴 | `measureActualRpm()` keine Einlaufzeit | Parcour bricht sofort ab | +3 Zeilen |
| X3 | 🔴 | `learnSGProfile()` SG bei Beschl./Bremsen | SGTHRS falsch gelernt | runSpeed() statt move() |
| X4 | 🔴 | `begin()` bei jedem setMotorPower() | Register-Reset vor jeder Funktion | begin() aus setMotorPower() raus |
| X5 | 🔴 | CORS-Header fehlt | telemetry_viewer.html funktioniert nicht | 1 Zeile |
| X6 | 🟡 | CSV 10 statt 14 Spalten | Charts im Viewer leer | Format angleichen |
| X7 | 🟡 | 3 verschiedene Versionsstrings | Debugging-Verwirrung | FW_VERSION verwenden |
| X8 | 🟡 | setMotorPower ohne Mutex von Core 0 | Theoretische Race Condition | pendingPower-Flag |

**Reihenfolge für Reparatur:** X1 → X2 → X4 → X5 → X3 → X6 → X7

---

## Review v3.4.2 — Stand der X-Bugs und neue Findings

**Geprüfte Dateien:** `MotorControl.cpp`, `MotorControl.h`, `main_v3.cpp`

### X-Bug Status

| # | Status | Nachweis |
|---|--------|---------|
| X1 | ✅ **behoben** | `xTaskCreatePinnedToCore(... updateTelemCache() ..., 0)` in `setup()` |
| X2 | ✅ **behoben** | 500ms Settle vor `pulseCount=0` in `measureActualRpm()` |
| X3 | ✅ **behoben** | `runSpeed()` + 800ms Einlauf + 1500ms Messphase in `learnSGProfile()` |
| X4 | ✅ **behoben** | `begin()` nur noch in `initMotors()`, `setMotorPower()` macht nur `toff()` |
| X5 | ✅ **behoben** | `res->addHeader("Access-Control-Allow-Origin", "*")` |
| X6 | ✅ **behoben** | CSV wieder 14 Spalten inkl. `cur_a, cur_b, otpw, ot` |
| X7 | ⚠️ **halb** | `FW_VERSION "3.4.2"` korrekt, `/status` gibt `fw` zurück — aber HTML zeigt noch hardcoded `"v3.3.9"` und JS liest `s.fw` nicht aus |
| X8 | ✅ **behoben** | `pendingPower` Flag in `SystemState`, WebServer setzt, TaskCore1 führt aus |

**6 von 8 vollständig behoben. X7 halb offen.**

---

### 🔴 Neu: N1 — UART Race Condition zwischen Core 0 und Core 1

**Das ist der kritischste verbleibende Bug.**

```
Core 0: TelemCache Task  → updateTelemCache() → driverX.SG_RESULT(), driverX.cs_actual(), ...
Core 1: Motor Task       → applyDriverSettings() → driverX.rms_current(), driverX.TPWMTHRS(), ...
```

Beide Kerne greifen auf dasselbe `driverX`-Objekt und damit auf `SERIAL_PORT` (Serial2) zu — ohne jeglichen Mutex. Serial2 ist **nicht thread-safe**. Wenn beide gleichzeitig senden/empfangen, kollisionieren die UART-Frames → korrupte TMC2209-Registerwerte, gelegentliche Garbage-SG-Werte in Telemetrie, im schlimmsten Fall fehlerhafte Motorparameter.

Das passiert besonders beim Start von Tests, wo `applyDriverSettings()` aufgerufen wird während der TelemCache-Task im 300ms-Zyklus läuft.

**Fix:** Alle `driverX.*()` Aufrufe in `updateTelemCache()` mit Critical Section schützen:
```cpp
void updateTelemCache() {
    portENTER_CRITICAL(&motorMux);
    tCache.sg  = driverX.SG_RESULT();
    tCache.cs  = driverX.cs_actual();
    // ... alle UART-Reads ...
    portEXIT_CRITICAL(&motorMux);
}
```
Gleiches gilt für `applyDriverSettings()`. Dann kann immer nur ein Kern gleichzeitig den UART nutzen.

---

### 🟡 N2 — `learnSGProfile`: UART-Call in jeder Loop-Iteration

```cpp
while(millis() - start < 1500) {
    steppers[0]->runSpeed();
    sgSum += (float)driverX.SG_RESULT(); // UART-Read in JEDER Iteration
    samples++;
    yield();
}
```
Die Schleife läuft ~10.000–50.000 mal pro Sekunde. `driverX.SG_RESULT()` ist eine UART-Transaktion die ~50–200µs dauert. Das blockiert `runSpeed()` und stört das Schrittgenerator-Timing. Außerdem `samples` zählt jeden Loop-Durchlauf — bei 10.000 Samples/Sekunde ist die Mittelung sinnlos und kostet CPU.

**Fix:** SG nur alle 50ms samplen:
```cpp
unsigned long lastSG = millis();
while(millis() - start < 1500) {
    steppers[0]->runSpeed();
    if (millis() - lastSG >= 50) {
        sgSum += (float)driverX.SG_RESULT(); samples++;
        lastSG = millis();
    }
    yield();
}
// → ~30 Samples pro Messpunkt statt 50.000
```

---

### 🟡 N3 — `runSpeedTest`: Startpunkt ignoriert `cal.maxRpm`

```cpp
float rpm = 200.0f; // immer 200 RPM, auch wenn maxRpm=440 bekannt
```
Früherer Fix (80% von `cal.maxRpm`) ist nicht mehr drin. Bei jedem Testlauf beginnt der Parcour von vorne bei 200 RPM — das dauert unnötig lange.

**Fix:** `float rpm = (sys.cal[0].maxRpm > 250.0f) ? sys.cal[0].maxRpm * 0.8f : 200.0f;`

---

### 🟡 N4 — `learnSGProfile`: kein `applyDriverSettings` nach dem Lernen

```cpp
sys.cal[0].sgThrs = ...;
saveCalibration(0);
driverX.en_spreadCycle(false); // ← setzt nur einen Parameter
// FEHLT: applyDriverSettings(...) mit den gerade gelernten Werten
```
Nach dem Lernlauf ist `sys.cal[0].sgThrs` neu gesetzt, aber der TMC2209 läuft noch mit dem alten SGTHRS-Wert im Register bis zum nächsten `applyDriverSettings`-Aufruf.

**Fix:** Am Ende von `learnSGProfile`:
```cpp
uint16_t cur = sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT;
applyDriverSettings(cur);
```

---

### 🟡 N5 — Versionsstring im HTML hardcoded (X7 Rest)

```html
<span class="version">V3 SINGLE-MOTOR v3.3.9 (Robust Fix)</span>  <!-- HTML -->
<div id="log">Bereit. v3.3.7 (ISR + High-Speed Merged)</div>        <!-- Log-Div -->
```
`/status` gibt korrekt `"fw":"3.4.2"` zurück, aber das JavaScript liest `s.fw` nicht aus. Anzeige bleibt immer "v3.3.9".

**Fix:** Im JS: `if(s.fw) document.querySelector('.version').innerText = 'V3 ' + s.fw;`
Und Log-Div-Text löschen oder ebenfalls dynamisch setzen.

---

### Gesamtübersicht v3.4.2

| # | Schwere | Status | Beschreibung |
|---|---------|--------|-------------|
| X1–X6, X8 | 🔴🟡 | ✅ behoben | Core-0-Task, Einlaufzeit, runSpeed(), begin(), CORS, CSV-Format, pendingPower |
| X7 | 🟡 | ⚠️ halb | FW_VERSION korrekt, aber HTML zeigt alten String |
| **N1** | 🔴 | ❌ offen | UART Race Condition Core 0 ↔ Core 1 (driverX ohne Mutex) |
| N2 | 🟡 | ❌ offen | SG-Read in jeder Loop-Iteration (zu häufig, stört Schrittgenerator) |
| N3 | 🟡 | ❌ offen | Parcour startet immer bei 200 RPM statt 80% von maxRpm |
| N4 | 🟡 | ❌ offen | learnSGProfile ohne applyDriverSettings am Ende |
| N5 | 🟢 | ❌ offen | Hardcoded Versionstring im HTML |

**Priorität:** N1 zuerst — UART Race Condition kann sporadische Motorausfälle und Garbage-Telemetrie verursachen.



### 🔴 Kritisch

**C1 — `learnSGProfile`: Messpunkte im StealthChop-Bereich, nur 1 Sample**

```cpp
uint32_t sps[] = {500, 1000, 2000, 4000};
// = 9.4 / 18.8 / 37.5 / 75 RPM → alles unter TPWMTHRS=200 RPM → StealthChop
// + nur 1 Sample NACH dem Move (Motor steht/bremst!) → SG≈0
sgSum += driverX.SG_RESULT();
```
Identischer Fehler wie v3.3.0. SGTHRS wird auf ~0–5 gesetzt → 43% false-stalls im Parcour.

**C2 — `learnSGProfile`: Integer-Truncation vor `*0.6`**

```cpp
sys.cal[0].sgThrs = (sgSum / 4) * 0.6;
// sgSum/4 = integer division → Abschneiden
// Beispiel: sgSum=30 → 30/4=7 (nicht 7.5) → 7*0.6=4.2 → uint8_t=4
// Korrekt: (uint8_t)((float)sgSum / 4 * 0.6f)
```

**C3 — `runSpeedTest`: `lastSensorPos % 3200` ohne Startposition-Offset**

```cpp
steppers[0]->move(6400);  // startet von triggerCenter (z.B. pos=80)
// ...
long drift = abs(lastSensorPos % 3200);
// Nach 6400 Steps korrekt bei pos=6480 → 6480%3200=80 → drift=80 > 60 → IMMER FAIL
// Der Check liefert triggerCenter%3200 auch ohne jeden Schrittverlust
```
Bei `triggerCenter > 60` schlägt der Drift-Check strukturell fehl — unabhängig vom Motorverhalten.
Korrekte Berechnung: `drift = abs((lastSensorPos - expectedSensorPos) % 3200)`

**C4 — `tachoISR`: nicht-ISR-sichere Funktion aufgerufen**

```cpp
void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) {
        lastSensorPos = steppers[0]->currentPosition(); // ← nicht ISR-sicher!
    }
}
```
`AccelStepper::currentPosition()` liest `_currentPos` ohne Mutex — wird gleichzeitig vom Step-Generator im Hauptkontext geschrieben. Auf ESP32 sporadische Race Condition → falsche `lastSensorPos`-Werte möglich.

---

### 🟡 Mittel

**M1 — `applyDriverSettings`: `en_spreadCycle(false)` fehlt**

```cpp
void applyDriverSettings(uint16_t runMA) {
    driverX.TPWMTHRS(tpwm);
    // FEHLT: driverX.en_spreadCycle(false);
}
```
`characterizeSensor`, `learnSGProfile`, `runSpeedTest` setzen `en_spreadCycle(true)` ohne Rücksetzung. Nach diesen Funktionen bleibt der Motor in permanentem SpreadCycle — TPWMTHRS hat dann keine Wirkung.

**M2 — `runSpeedTest`: `maxRpm` falsch gespeichert**

```cpp
sys.cal[0].maxRpm = rpm - PARCOUR_RPM_STEP;
// Falls Schleife durch rpm > PARCOUR_RPM_MAX endet (kein Fehler):
// maxRpm = 2500 - 100 = 2400 RPM — obwohl Motor real bei ~440 RPM limitiert ist
```

**M3 — `runInertiaTest`: kein `setMaxSpeed` vor Test**

```cpp
void runInertiaTest(int i) {
    while(acc <= 40000 && !failed) {
        steppers[0]->setAcceleration(acc);
        steppers[0]->move(3200);
        // setMaxSpeed nie gesetzt → verwendet initMotors-Default: 4000 sps = 75 RPM
        // Test läuft immer bei 75 RPM, ignoriert cal.maxRpm
```

**M4 — `runCoastTest`: kein Guard, ignoriert Parameter `i`**

```cpp
void runCoastTest(int i) {
    setMotorPower(0, true);  // ignoriert i, hardcoded Motor 0
    // FEHLT: if (i != 0 || !sys.cal[0].valid) return;
    // Läuft bei nicht kalibriertem Motor mit hardcoded 400 RPM
```

---

### 🟢 Minor

**m1 — `updateMotors`: GPIO-Write jede Millisekunde**

```cpp
if (sys.m[0].enabled) {
    digitalWrite(ENABLE_PIN, LOW); // Unnötig: jede ms, kostet ~1 µs CPU
    steppers[0]->run();
}
```

**m2 — `lastSensorPos` ohne Memory-Barrier**
`volatile long` schützt vor Compiler-Optimierung, aber nicht vor CPU-Reordering zwischen ISR und Main-Task auf ESP32 mit FreeRTOS. Besser: `portENTER_CRITICAL_ISR` beim Lesen im Hauptkontext.

---

### Übersicht

| # | Schwere | Funktion | Problem | Auswirkung |
|---|---------|----------|---------|------------|
| C1 | 🔴 | `learnSGProfile` | StealthChop-Bereich + 1 Sample beim Stillstand | SGTHRS≈0 → false-stalls |
| C2 | 🔴 | `learnSGProfile` | Integer-Division vor `*0.6` | SGTHRS bis 20% zu niedrig |
| C3 | 🔴 | `runSpeedTest` | `lastSensorPos%3200` ohne Offset | Drift-Check bei triggerCenter>60 immer FAIL |
| C4 | 🔴 | `tachoISR` | `currentPosition()` nicht ISR-sicher | Sporadisch falsche Sensorpositionen |
| M1 | 🟡 | `applyDriverSettings` | `en_spreadCycle` nicht zurückgesetzt | TPWMTHRS wirkungslos nach Tests |
| M2 | 🟡 | `runSpeedTest` | `maxRpm` falsch bei vollem Durchlauf | Nächster Parcour-Start falsch |
| M3 | 🟡 | `runInertiaTest` | kein `setMaxSpeed` | Test immer bei 75 RPM |
| M4 | 🟡 | `runCoastTest` | kein Guard, ignoriert Parameter | Läuft ohne Kalibration |
| m1 | 🟢 | `updateMotors` | GPIO-Write jede ms | CPU-Last, kein Funktionsproblem |
| m2 | 🟢 | `tachoISR` / Main | `lastSensorPos` ohne Memory-Barrier | Theoretische Race Condition |

---

## Nächste Schritte (priorisiert)

### 1. TPWMTHRS verifizieren
Nach `LEARN SG PROFILE` im Log nachsehen:
```
SGTHRS=X TPWMTHRS=Y (Z RPM)
```
Liegt Z unter 300 RPM? → Motor läuft bei Testdrehzahl in SpreadCycle ✓
Liegt Z über 300 RPM? → Motor läuft in StealthChop → Jitter durch Resonanz wahrscheinlich

### 2. Lernlauf explizit in SpreadCycle durchführen
`learnSGProfile()` sollte für SG-Messung explizit SpreadCycle erzwingen, da SG im SpreadCycle reproduzierbarer ist.

### 3. Mechanik prüfen
Dreht der Motor per Hand gleichmäßig? Gibt es Rastmoment-Unregelmäßigkeiten? Pancake-Motoren haben oft stärkeres Rastmoment relativ zum Haltemoment.

### 4. Telemetrie-Vergleich nach TPWMTHRS-Fix
Nach nächstem Lauf: SG_RESULT in SPEED-Phase sollte deutlich über 50 liegen wenn SpreadCycle aktiv.

---

## Referenz-Schwellwerte (Zielwerte für "gesunden" Lauf)

| Metrik | Aktuell | Ziel |
|--------|---------|------|
| SG_RESULT (SPEED) | 0–46 | > 100 |
| Stall-Events/Lauf | 19–211 | < 5 |
| Max RPM (stabil) | 300–440 | > 1000 |
| cs_actual | 18–26 | 25–32 |
| Drift nach 2 Umdr. | > 60 Steps | < 20 Steps |

---

## Datei-Referenz

| Datei | FW | Datum | Besonderheit |
|-------|----|-------|-------------|
| `v3/tele/parcour_183755.csv` | 3.2.0 | 2026-03-24 | Baseline, fester Strom |
| `v3/tele/parcour_151061.csv` | 3.3.0 | 2026-03-24 | Erster Lauf mit adapt. Strom, SGTHRS-Problem |
| `v3/tele/parcour_393983.csv` | 3.3.0 | 2026-03-24 | Kurzlauf, abgebrochen |

---

## Fix v3.4.5 — N1–N5 behoben (2026-03-24)

### N1 — UART Race Condition ✅ behoben

`updateTelemCache()` lief auf Core 0, `applyDriverSettings()` (driverX UART) auf Core 1 — kein Mutex.

**Fix:** `portENTER_CRITICAL(&motorMux)` / `portEXIT_CRITICAL` um alle driverX-Reads in `updateTelemCache()`.

```cpp
void updateTelemCache() {
    portENTER_CRITICAL(&motorMux);
    tCache.sg = driverX.SG_RESULT();
    // ... alle UART-Reads ...
    portEXIT_CRITICAL(&motorMux);
}
```

### N2 — SG-Read zu häufig ✅ behoben

`learnSGProfile()` rief `driverX.SG_RESULT()` direkt in jedem Loop-Durchlauf auf → tausende UART-Reads/Sekunde.

**Fix:** Liest jetzt `tCache.sg` (Cache, nicht direkt UART) alle 50ms:

```cpp
if(millis() - lastSample >= 50) {
    sgSum += (float)tCache.sg; samples++;
    lastSample = millis();
}
```

### N3 — Parcour startet immer bei 200 RPM ✅ behoben

`runSpeedTest()` ignorierte `cal[0].maxRpm` — jeder Lauf startete bei 200 RPM, auch wenn Motor schon bis 1000 RPM bekannt war.

**Fix:** Dynamischer Startpunkt:

```cpp
float rpm = (sys.cal[0].maxRpm > 250.0f) ? sys.cal[0].maxRpm * 0.8f : 200.0f;
```

### N4 — learnSGProfile ohne applyDriverSettings am Ende ✅ behoben

Nach `learnSGProfile()` blieb `en_spreadCycle` auf `false` gesetzt aber SGTHRS war neu (niedriger Wert) — nachfolgende Fahrt hatte falsche StallGuard-Empfindlichkeit.

**Fix:** `applyDriverSettings()` am Ende von `learnSGProfile()` aufrufen (schreibt neues SGTHRS auf den Chip):

```cpp
applyDriverSettings(sys.cal[0].learnedCurrentMA > 0 ? sys.cal[0].learnedCurrentMA : MOTOR_CURRENT_DEFAULT);
```

### N5 — Hardcoded Versionstring im HTML ✅ behoben

HTML-Navbar zeigte immer "V3 SINGLE-MOTOR v3.4.3 (16-MS Fix)" unabhängig von der echten FW-Version.

**Fix:** Initialer Text auf "..." gesetzt, JS überschreibt mit `s.fw` beim ersten `/status`-Poll (Zeile 103 war schon korrekt).

### FW_VERSION

`3.4.4` → `3.4.5`

### Status nach v3.4.5

| Bug | Status |
|-----|--------|
| N1 UART Race Condition | ✅ behoben |
| N2 SG-Read Frequenz | ✅ behoben |
| N3 Parcour Start RPM | ✅ behoben |
| N4 applyDriverSettings nach Learn | ✅ behoben |
| N5 Hardcoded Versionstring | ✅ behoben |
| X7 HTML version (initial) | ✅ behoben |

**Alle bekannten Bugs behoben. Bereit zum Flashen.**

Nächster Schritt: `python build_flash_v3.py ota`
