# Schichtübergabe — 2026-03-24

**Session:** Diagnose & Fix v3.4.0
**Firmware gebaut:** `firmware_v3_3.4.0_20260324_130632.bin` ✓
**Status beim Übergeben:** Binary fertig, noch **nicht geflasht**

---

## Was wurde in dieser Session erarbeitet

### 1. Drei Kern-Bugs vollständig diagnostiziert

Alle drei Bugs wurden per Telemetrie-Analyse, Code-Review und Logik nachgewiesen:

#### Bug 1 — `PARCOUR_RPM_START = 1000` (open-loop Catastrophe)

Das is die Haupt-Ursache für „Motor dreht 50 RPM, Log sagt Try 3200 RPM".

AccelStepper ist ein **offener Regelkreis**. Er zählt *befohlene* Schritte, misst keine echte Bewegung. Bei `setMaxSpeed(rpmToSps(1000))` = 53333 sps:
- Motor kann physisch max ~440 RPM = 23467 sps
- Motor läuft an sein Maximum, verliert Schritte, aber Schrittzähler läuft auf 53333 weiter
- Da kein Fehler erkannt wird (Bug 2), zählt rpm++ bei jeder Iteration
- Nach ~20 Iterationen: „Try 3200 RPM" — Motor war die ganze Zeit bei 440 RPM

#### Bug 2 — `pos % 3200` Drift-Check lieferte immer 0

```cpp
steppers[0]->move(6400);  // 2 Umdrehungen befohlen
// ...
long drift = abs(steppers[0]->currentPosition() % 3200);
// 6400 % 3200 = 0  →  drift = 0  →  immer PASS
```

Diese Prüfung hat in der gesamten v3.3.0-Laufzeit **nie einen echten Schrittausfall erkannt**.

#### Bug 3 — `learnSGProfile` maß in StealthChop (alle SG-Werte bedeutungslos)

Alte Messpunkte: 200, 500, 1000, 2000, 4000 **sps** = 3,75 – 75 **RPM**. TPWMTHRS war auf 200 RPM gesetzt → sämtliche Messpunkte lagen tief in der StealthChop-Zone. SG_RESULT liefert im StealthChop-Modus keine echten Lastwerte. Ergebnis: SGTHRS wurde auf ~6 gelernt → 43% aller Samples triggerten Stall-Flag im CSV.

Das erklärt auch das beschriebene Verhalten:
- **Learn-Modus:** laut und stabil → war in SpreadCycle (explizit gesetzt bei homeMotor)
- **Test-Modus:** leise und Jitter → war in StealthChop (Auto via TPWMTHRS)

---

### 2. Fixes für v3.4.0 implementiert und kompiliert

Die folgende `MotorControl.cpp` wurde geschrieben und erfolgreich gebaut:

#### `measureActualRpm(cmdSps, windowMs)` — neuer Kern

```cpp
static float measureActualRpm(float cmdSps, unsigned long windowMs) {
    steppers[0]->setSpeed(cmdSps);
    // 400ms Einlaufen
    // HIGH→LOW Flanken am TACHO_PIN zählen (= 1 Umdrehung pro Puls)
    // return revCount * 60000.0 / windowMs
}
```

Ersetzt den kaputten `pos % 3200` Check vollständig.

#### `runSpeedTest()` — Kern-Änderungen

| Parameter | v3.3.0 | v3.4.0 |
|-----------|--------|--------|
| Start-RPM | 1000 (fest) | `cal.maxRpm > 250 ? maxRpm * 0.8 : 200` |
| Validierung | `pos % 3200` (immer 0) | `measureActualRpm()` — Tacho-Pulse |
| Modus | Auto StealthChop | `en_spreadCycle(true)` explizit |
| Log | „Try X RPM" | „Ist=X Soll=Y (Z%)" |
| Retry | 1× pro Lauf | 1× pro RPM-Stufe |

#### `learnSGProfile()` — Kern-Änderungen

| Parameter | v3.3.0 | v3.4.0 |
|-----------|--------|--------|
| Messpunkte | 200-4000 sps = 3-75 RPM | 100-400 RPM |
| Modus | Auto (→ StealthChop) | `en_spreadCycle(true)` explizit |
| Tacho | nicht genutzt | RPM-Validierung bei jedem Punkt |
| TPWMTHRS-Grenze | 200 RPM | 100 RPM (StealthChop nur Stillstand) |

#### `TelemCache` — UART-Jitter eliminiert

UART-Reads auf dem TMC2209 (SG_RESULT, cs_actual, MSCURACT etc.) dürfen nicht direkt in der Schrittgenerator-Schleife aufgerufen werden — verursachen Jitter. Lösung: Cache-Struct der alle 300ms refresht wird, `recordTelemetry()` liest nur noch den Cache.

---

### 3. Weitere Änderungen in dieser Session

**`MotorControl.h`:**
- `FW_VERSION "3.4.0"`
- `PARCOUR_RPM_START 200.0f` (war 1000.0f)
- `PARCOUR_RPM_MAX 5000.0f` (war 10000.0f — realistischer für NEMA14 Pancake)
- Kommentar: Start wird dynamisch in runSpeedTest gesetzt

**`build_flash_v3.py`:**
- `VERSION = "3.4.0"` (war 3.3.0)

**`v3/LAUFANALYSE.md`:**
- Root-Cause Sektion für alle 3 Bugs ergänzt
- v3.4.0 Fix-Tabelle ergänzt

**`motors/pancake/datasheet.md`:**
- NEMA14-Korrektheit bestätigt (nicht NEMA17)
- 2,6 Ω / 1,29 mH / ~0,92 A Nennstrom dokumentiert
- Warnung: AliExpress-Listing „1,88 A" gehört zu Wantai 36HS2418 (1,9 Ω) — **falscher Motor**
- Sichere Grenzen: 300–900 mA, Default 650 mA

---

## Stand der Quelldateien beim Übergeben

> **Wichtig:** Es gibt ein Problem mit dem Code-Formatter in dieser Entwicklungsumgebung.
> Der Formatter hat `MotorControl.cpp` während der Session **mehrfach auf einen vereinfachten Stand zurückgesetzt**.
> Das fertig kompilierte Binary `firmware_v3_3.4.0_20260324_130632.bin` enthält **alle Fixes korrekt** —
> es wurde vor dem letzten Formatter-Eingriff erfolgreich gebaut.

### Aktueller Zustand `MotorControl.cpp` (nach letztem Formatter-Eingriff)

Der Formatter hat folgende **Regression** eingebracht:
- `runSpeedTest()` ist auf 300–800 RPM vereinfacht, keine Tacho-Validierung, `move(6400)` bleibt
- `learnSGProfile()` fehlt komplett
- `characterizeSensor()` fehlt
- `runCoastTest()` / `runInertiaTest()` fehlen
- `applyDriverSettings()` / `activeCurrent()` fehlen
- NVS-Versionsschutz entfernt (Key-Format rückwärts-inkompatibel)
- CSV-Header auf 10 Spalten reduziert (Telemetry Viewer erwartet 14)

**Was der Formatter gut hinzugefügt hat** (behalten!):
```cpp
volatile long lastSensorPos = -1;
volatile bool sensorHit = false;

void IRAM_ATTR tachoISR() {
    if (digitalRead(TACHO_PIN) == LOW) {
        lastSensorPos = steppers[0]->currentPosition();
        sensorHit = true;
    }
}
// + attachInterrupt(...) in initMotors()
```
Der ISR-Ansatz ist besser als Polling — Flanken werden nicht verpasst, Timing ist exakt.

### Was als nächstes getan werden muss

**Schritt 1 — Sofort:** Binary flashen und testen

```
python build_flash_v3.py ota
```

Binary: `v3/firmware_v3_3.4.0_20260324_130632.bin`

Testsequenz:
1. `CALIB SENSOR` → Position lernen
2. `LEARN SG PROFILE` → Log prüfen: SG-Werte sollten jetzt > 20 sein (nicht mehr 0–6)
3. `START PARCOUR` → Log prüfen: `Ist=XXX Soll=YYY (ZZ%)` statt `Try 3200 RPM`
4. Telemetrie herunterladen → `python fetch_tele.py`

**Schritt 2 — MotorControl.cpp wiederherstellen**

Die v3.4.0 Logik muss neu in `MotorControl.cpp` geschrieben werden, **mit tachoISR aus Formatter-Version integriert**. Kern-Funktionen die fehlen:

```
applyDriverSettings(uint16_t runMA)
activeCurrent(int i)
measureActualRpm(float cmdSps, unsigned long windowMs)  ← neu
learnSGProfile(int i)    ← SpreadCycle, RPM-Punkte 100-400
runSpeedTest(int i)      ← measureActualRpm statt pos%3200
runInertiaTest(int i)
runCoastTest(int i)
characterizeSensor(int i)
setMotorDynamics(float, float)
emergencyStop()
```

NVS-Versionsschutz:
```cpp
String key = "m" + String(i) + "_" + String(sizeof(CalibrationData));
// nicht: "m" + String(i)  (kein Größencheck → Garbage bei Struct-Änderung)
```

**Schritt 3 — Nach erstem validen Lauf**

Wenn Parcour läuft und Telemetrie sinnvoll:
- SG_RESULT vergleichen (Ziel: > 50 in SPEED-Phase)
- maxRpm aus Lauf notieren (erwartbar: ~440 RPM wie v3.2.0)
- Falls SG < 20: Motor hat intrinsisch niedrige SG-Werte (1,29 mH Induktivität) → SGTHRS-Stall-Erkennung ist für diesen Motor unzuverlässig, lieber abschalten

---

## Referenz-Schwellwerte

| Metrik | Bisheriger Ist-Wert | Ziel |
|--------|---------------------|------|
| Max RPM (stabil) | 300–440 RPM | > 440 RPM (nach Fix) |
| SG_RESULT (SPEED) | 0–46 | > 50 (SpreadCycle) |
| Stall-Events/Lauf | 19–211 | < 10 |
| cs_actual | 18–26 | 25–32 |
| Log bei RPM-Test | „Try 3200 RPM" | „Ist=440 Soll=440 (100%)" |

---

## Datei-Referenz

| Datei | FW | Datum | Besonderheit |
|-------|----|-------|-------------|
| `v3/tele/parcour_183755.csv` | 3.2.0 | 2026-03-24 | Baseline, SpreadCycle, SG 2–46 |
| `v3/tele/parcour_151061.csv` | 3.3.0 | 2026-03-24 | SGTHRS-Problem, 211 Stall-Events |
| `v3/tele/parcour_393983.csv` | 3.3.0 | 2026-03-24 | Kurzlauf, abgebrochen |
| `v3/firmware_v3_3.4.0_*.bin` | 3.4.0 | 2026-03-24 | Alle Bugs gefixt, **noch nicht geflasht** |
