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
