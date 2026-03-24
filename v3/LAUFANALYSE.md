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

## Bug-Report v3.3.6 (aktueller Codestand)

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
