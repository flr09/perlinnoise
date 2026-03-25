# Schichtübergabe — 2026-03-24

**Session:** Diagnose & Fix v3.5.4
**Firmware gebaut:** `firmware_v3_3.5.4_20260324_181741.bin` ✓
**Status beim Übergeben:** Binary fertig, noch **nicht geflasht**

---

## Was wurde in dieser Session erarbeitet

### v3.5.2 — FastAccelStepper-Stabilisierung

Der Linter hatte in der Vorsession die Stepper-Bibliothek auf FastAccelStepper migriert (Hardware-Timer, besser für hohe sps). Die Migration brachte 5 neue Bugs mit sich, die in v3.5.2 behoben wurden:

- ISR-Crash (`tachoISR` rief nicht-ISR-sichere Funktion)
- Mutex-Timeout zu kurz (10ms statt 100ms)
- UART-Reads von Core 1 (→ auf `tCache.sg` umgestellt)
- `updateTelemCache` im Step-Loop (→ Jitter)
- Kein Settle vor Messung in `runSpeedTest`

Feature: Winkelmarker 90/180/270/360° in UI (orangene LED-Punkte ±8°)

### v3.5.3 — Boot-Crash und 4 Logik-Bugs

| Bug | Beschreibung | Fix |
|-----|--------------|-----|
| B1 Boot-Crash | `xSemaphoreCreateMutex()` global → FreeRTOS-Heap nicht bereit | In `initMotors()` verschoben |
| B2 learnSGProfile | Kein `stopMove()` → Motor läuft weiter → `setMicrosteps(64)` übersprungen | `stopMove()` vor setMicrosteps |
| B3 runCoastTest | Kein `stopMove()` → Motor läuft endlos nach Test | `stopMove()` am Ende |
| B4 float→uint32_t | Implicit Cast in `setSpeedInHz` | Expliziter `(uint32_t)` Cast |
| B5 NULL-Check | `waitForSensorTimed` ohne Null-Check für stepper | `if (!stepper) return false` |

### v3.5.4 — Button-Bugs characterizeSensor und learnSGProfile

**Ursache der gemeldeten „Buttons haben nicht die erwartete Funktion":**

#### D1 — `characterizeSensor` (falsche Kantenerkennung)

Nach `setSpeedInHz(200)` wurde weder `runForward()` noch `runBackward()` aufgerufen.
FastAccelStepper erfordert nach jedem Speed-Change einen expliziten Richtungsbefehl —
sonst läuft der Motor mit der alten Geschwindigkeit (400Hz) weiter.

Folge: `eCW` und `sCCW` wurden beim falschen Zeitpunkt erfasst →
falscher `triggerCenter` → **HOME MOTOR fuhr zu falscher Position**.

Fix:
```cpp
stepper->setSpeedInHz(200); stepper->runForward(); waitForSensorTimed(HIGH, 1000, 3000);
// bzw. CCW:
stepper->setSpeedInHz(200); stepper->runBackward(); waitForSensorTimed(HIGH, 1000, 3000);
```

#### D2 — `learnSGProfile` (runForward im Loop)

`stepper->runForward()` wurde in **jeder Iteration** der inneren Messschleife aufgerufen (~jede paar ms). FastAccelStepper interpretiert jeden `runForward()`-Aufruf als neuen Bewegungsbefehl → Motor-Timing-Unterbrechungen / Jitter alle paar ms während SG-Messung.

Fix: `runForward()` einmalig vor den `while`-Block verschoben.

---

## Auswirkung der v3.5.4-Bugs

| Funktion | Symptom | Root Cause |
|----------|---------|------------|
| **CALIB SENSOR** | HOME fuhr zur falschen Position | D1: triggerCenter falsch wegen falscher Geschwindigkeit |
| **SG-LEARN** | Jitter, unzuverlässige SG-Werte | D2: runForward() pro Iteration |
| **START PARCOUR** | Lief mit falschem Zentrum | D1: cal[0].valid=true, aber Wert falsch |

---

## Stand der Quelldateien beim Übergeben

### Aktueller Zustand (v3.5.4, unkompromittiert)

- `v3/src/MotorControl.h`: `FW_VERSION "3.5.4"`
- `v3/src/MotorControl.cpp`: D1 + D2 gefixt
- `build_flash_v3.py`: `VERSION = "3.5.4"`
- `v3/LAUFANALYSE.md`: v3.5.2, v3.5.3, v3.5.4 dokumentiert
- `v3/BUG_REPORT_V3.md`: Alle Bugs A1–D2 dokumentiert

### Was als nächstes getan werden muss

**Schritt 1 — Binary flashen:**

```
python build_flash_v3.py ota
```

Binary: `v3/firmware_v3_3.5.4_20260324_181741.bin`

**Testsequenz nach Flash:**

1. `CALIB SENSOR` → triggerCenter prüfen (Logausgabe)
2. `HOME MOTOR` → Motor muss zur korrekten 0°-Position fahren
3. `SG-LEARN` → Log: SG-Werte sollten gleichmäßig und > 20 sein (kein Jitter)
4. `START PARCOUR` → Läuft durch, kein Motor-Drift durch falsches Zentrum
5. Telemetrie: `python fetch_tele.py`

**Schritt 2 — Nach erstem validen Lauf:**

- SG_RESULT vergleichen (Ziel: > 50 in SPEED-Phase)
- maxRpm notieren (erwartet: ~440 RPM)
- Falls SG < 20: SGTHRS-Stall-Erkennung für diesen Motor deaktivieren (1,29 mH zu schwach)

---

## Datei-Referenz

| Datei | FW | Datum | Besonderheit |
|-------|----|-------|--------------|
| `v3/tele/parcour_183755.csv` | 3.2.0 | 2026-03-24 | Baseline, SpreadCycle, SG 2–46 |
| `v3/tele/parcour_151061.csv` | 3.3.0 | 2026-03-24 | SGTHRS-Problem, 211 Stall-Events |
| `v3/firmware_v3_3.5.4_*.bin` | 3.5.4 | 2026-03-24 | D1+D2 gefixt, **noch nicht geflasht** |

---

## Referenz-Schwellwerte

| Metrik | Bisheriger Ist-Wert | Ziel |
|--------|---------------------|------|
| Max RPM (stabil) | 300–440 RPM | > 440 RPM |
| SG_RESULT (SPEED) | 0–46 | > 50 (SpreadCycle) |
| Stall-Events/Lauf | 19–211 | < 10 |
| cs_actual | 18–26 | 25–32 |
| HOME-Position | falsch (v3.5.3) | korrekt (v3.5.4) |
