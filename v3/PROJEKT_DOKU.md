# PerlinNoise v3 — Projektdokumentation

**Hardware:** FYSETC E4 · ESP32 · TMC2209 · NEMA17 (36BYG1204-A-6QHT, Pancake)
**Sensor:** Induktiver Näherungssensor AZ-Delivery LJ12A3-4-Z/BX an GPIO 15 (TACHO_PIN) — wird später gegen anderes Modell getauscht
**Stand:** 2026-03-27 | aktuell: v3.7.1

---

## 1. Hardware

### Pins (FYSETC E4)
| Signal | GPIO |
|--------|------|
| X_STEP | 27 |
| X_DIR  | 26 |
| ENABLE | 25 |
| UART RX | 21 |
| UART TX | 22 |
| TACHO_PIN | 15 |

### Sensor — LJ12A3-4-Z/BX (induktiv, NPN NO)

| Eigenschaft | Wert |
|-------------|------|
| Typ | Induktiver Näherungssensor |
| Schaltlogik | NPN, Normally Open (NO) |
| Versorgung | 6–36V DC |
| Erfassungsbereich | 4 mm (Metall) |
| Ausgang | Open Collector: LOW wenn Metall erkannt, floating sonst |
| ESP32-Anschluss | Pull-up intern auf 3,3V — GPIO liest LOW = getriggert |

**Spannungssicherheit:** NPN-Ausgang sinks nur gegen GND — kein Spannungsübertrag an ESP32-GPIO, solange Pull-up auf 3,3V. Versorgung des Sensors (z. B. 12V) spielt keine Rolle.

**Bibliotheken:**
- **[Bounce2](https://github.com/thomasfredericks/Bounce2)** (`thomasfredericks/Bounce2 @ ^2.71`) — aktiv in `waitForSensorTimed` (Homing/Calib-Sensorsuche). Debounce-Intervall 5 ms, deckt sich mit ISR-Noise-Filter.
- **ISR (`tachoISR`):** Bounce2 nicht anwendbar (nicht ISR-safe). Bleibt direkter Hardware-Register-Read mit eigenem 5 ms-Filter.

**Geplanter Tausch:** Sensor wird später gegen anderes Modell ersetzt — NVS-Kalibrierung bleibt kompatibel (Grad-basiert, unabhängig von Sensorbreite).

### Hardware-Vorfälle
**2026-03-24 — PNP statt NPN am TACHO_PIN:**
Ein PNP-Schalter wurde irrtümlich an GPIO 15 betrieben. PNP schaltet Versorgungsspannung auf den Pin — ESP32 ist nur 3,3V-tolerant. Risiko: GPIO 15 intern beschädigt. Inzwischen durch korrekten NPN-Schalter (LJ12A3-4-Z/BX) ersetzt. Validierung: bei Metall in Erfassungsbereich muss Web-UI "HIT" zwischen TRUE/FALSE wechseln.

### Single-Motor-Modus
Derzeit nur Motor X (Index 0) aktiv. Y/Z/E-Treiber instanziiert, aber nicht konfiguriert.
Reaktivierung: `initMotors()` in MotorControl.cpp, Loop-Limits in main_v3.cpp auf 4 setzen.

---

## 2. Build & Flash

**Toolchain:** PlatformIO in WSL venv
**PIO-Pfad:** `/home/chris/perlinnoise/.venv/bin/platformio`
**Konfiguration:** `platformio.ini` im Projekt-Root, `build_src_filter` trennt v2/v3

```bash
# Nur bauen
python build_flash_v3.py

# USB flashen (COM4)
python build_flash_v3.py flash

# OTA (perlin-v3.local)
python build_flash_v3.py ota
```

Artefakte: `.pio/build/fysetc_e4_v3/firmware.bin` → wird nach `v3/` kopiert.
Web-OTA: `http://[IP]/update`

---

## 3. Architektur

### ESP32 Dual-Core
- Core 0: WiFi / Web-Server
- Core 1: Motor-Tasks (FreeRTOS)

### FastAccelStepper (FAS)
`runForward/Backward()`, `move()`, `moveTo()`, `stopMove()`, `isRunning()`, `getCurrentSpeedInMilliHz()`
**Kritisch:** `stopMove()` löst nur Deceleration aus — Motor bewegt sich noch bis `isRunning()==false`.
Nach jedem `stopMove()` muss `while(stepper->isRunning()) { yield(); }` folgen bevor neue Befehle.

### PCNT (Hardware-Pulszähler)
Zählt X_STEP-Pulse mit Richtungserkennung (X_DIR). ISR-safe: direkt `PCNT.cnt_unit[0].val` lesen.
`pcntStepperBase` = FAS-Position beim letzten PCNT-Clear. Absolute Sensor-Position = `lastSensorRaw + pcntStepperBase`.
**Initialisierungsreihenfolge:** PCNT vor FAS — `pcnt_unit_config()` ruft `gpio_output_disable()` auf, was RMT-Routing von FAS überschreiben würde.

### Tacho-ISR
CHANGE-Interrupt auf TACHO_PIN. LOW-Flanke = 1 Umdrehung. Periode zwischen LOW-Flanken = `tachoPeriodMs`.
Noise-Filter: nur updaten wenn Periode ≥ 5 ms (eliminiert Bounce).
`getTachoRpm()`: gibt 0 wenn `tachoPeriodMs==0` oder letzter Puls >2 s her.

### TMC2209 / StallGuard
- SG_RESULT nur verwertbar im SpreadCycle-Modus (`cs_actual > 0`, oberhalb TCOOLTHRS, >~600 RPM)
- `TPWMTHRS=0` erzwingt StealthChop bei allen Geschwindigkeiten
- `en_spreadCycle(false)` + `TPWMTHRS>0` → Auto-Switch TMC intern
- `applyDriverSettings(runMA)` muss nach `setMicrosteps()` aufgerufen werden (nutzt aktuelles `stepsPerRev`)

### Koordinatensystem (ab v3.6.19 — Grad-absolut)
```
  CCW ←──────────────────────────────── CW
        triggerStart      triggerEnd
             │                 │
  ───────────┼──── SENSOR ─────┼────────
          sCW (ON)         eCW (OFF)
                    ↑ CENTER = 0° ↑
```
- `triggerStartDeg` = `stepsToDeg(a1 − center)` (negativ, z. B. −2.8°)
- `triggerEndDeg`   = `stepsToDeg(a2 − center)` (positiv, z. B. +2.8°)
- Alle Winkelwerte in `CalibrationData` als `float` Grad — **nie als Schritte**.
- `nvsVersion = 3619` invalidiert ältere NVS-Strukturen automatisch bei Ladezeit.

### Driver-Layer Winkel-API (ab v3.7.1)

Alle Module greifen ausschließlich über diese Funktionen auf Motorpositionen zu.
Kein Modul außerhalb von Driver.cpp/h darf `stepsPerRev` direkt multiplizieren oder `stepper->moveTo(steps)` mit inline-Konvertierung aufrufen.

```cpp
// Driver.h (inline — immer aktuelles stepsPerRev)
long  degToSteps(float deg)   // deg * stepsPerRev / 360
float stepsToDeg(long steps)  // steps * 360 / stepsPerRev

// Driver.cpp
void  moveToDeg(float deg)      // stepper->moveTo(degToSteps(deg))
void  moveByDeg(float deg)      // stepper->move(degToSteps(deg))
void  setPositionDeg(float deg) // stepper->setCurrentPosition(degToSteps(deg))
float getPositionDeg()          // stepsToDeg(stepper->getCurrentPosition())
```

Vorteil: Microstep-Wechsel (z. B. 16MS → 64MS) skaliert `stepsPerRev` atomisch in `setMicrosteps()`.
Alle nachgelagerten Berechnungen (`moveToDeg`, `degToSteps`) nutzen automatisch den neuen Wert — keine verstreuten `deg * stepsPerRev / 360`-Ausdrücke.

### Kalibrierung — characterizeSensor (ab v3.7.1)
```
  CCW ←──────────────────────────────────────────── CW →
                              Motor fährt →→→→→→→→→→
  ──────────┬─── Anlauf ───┬───────SENSOR────┬──────────
            │  CCW 0.3rev  │   A1(ON)  A2(OFF)│
            └──────────────┘         ↑
                                  Center = 0°
```
1. **P0** (wenn nötig): Sensor bereits aktiv → CW bis OFF (aus Sensor herausfahren)
2. CCW 500 sps, 0.3 rev Anlaufstrecke (reproduzierbarer Startpunkt links von A1)
3. PCNT reset — `pcntStepperBase = stepper->getCurrentPosition()`
4. **A1:** CW 500 sps bis Sensor ON → ISR erfasst `a1` exakt
5. **A2:** CW 500 sps weiter bis Sensor OFF → ISR erfasst `a2` exakt
6. `stopMove()` + `while(isRunning())`
7. `center = lroundf((a1 + a2) / 2.0f)` — gerundete Mitte (kein Integer-Truncation-Fehler bei ungeradem Abstand)
8. `triggerStartDeg = stepsToDeg(a1 − center)`, `triggerEndDeg = stepsToDeg(a2 − center)` → NVS
9. `moveTo(center)` → `setPositionDeg(0.0f)` → Sensormitte ist 0°

### Homing — homeMotor (ab v3.7.1)
1. CW 1200 sps bis Sensor ON — Schnellsuche, max. 1,5 rev
2. `stopMove()` + `while(isRunning())` — Motor vollständig stoppen
3. CCW 400 sps bis Sensor OFF — Sensor verlassen
4. `stopMove()` + `while(isRunning())`
5. PCNT reset — `pcntStepperBase = stepper->getCurrentPosition()`
6. CW 200 sps bis Sensor ON → ISR erfasst `a1_abs` (exakte Einschaltkante)
7. `stopMove()` + `while(isRunning())` — **muss vollständig stehen vor setCurrentPosition!**
8. `overshoot = curPos − a1_abs`
9. `setCurrentPosition(degToSteps(triggerStartDeg) + overshoot)` → `moveToDeg(0.0f)` → Sensormitte = 0°

---

## 4. Gemessene physikalische Grenzen

### Geschwindigkeit (Motor X, 16MS, SpreadCycle)
| Wert | RPM | SPS | Status |
|------|-----|-----|--------|
| Verifiziertes Maximum | 2500 RPM | 133.333 | ✅ 0 Stalls |
| SG_RESULT @ 2500 RPM | — | — | ~22 (dünn aber OK) |
| SG_RESULT @ 2000 RPM | — | — | ~130 (komfortabel) |
| Empfohlenes Dauermax | ~2300 RPM | ~122.667 | SG ~60 |

SG_RESULT = 22 bei 2500 RPM: Motor läuft bereits in SpreadCycle (TPWMTHRS überschritten) — kein echter Stall-Risk für kurze Positionswechsel. Für Dauerlauf >10 s: 2300 RPM sicherer.

### Beschleunigung (Revs bis 2500 RPM @ 16MS)
| Accel sps² | Umdrehungen Hochlauf |
|------------|---------------------|
| 2.000 | 2.963 |
| 30.000 | 197 |
| 100.000 | 59 |
| 500.000 | 12 |

Formel: `Umdrehungen = v² / (2 × a × stepsPerRev)` mit v=133333, stepsPerRev=3200

### Microstep-Strategie (TMC2209 interpoliert intern immer auf 256 MS)
| MRES | SPS @ 2500 RPM | Revs Hochlauf (a=100k) |
|------|---------------|------------------------|
| 64 MS | 533.333 | 2.370 ❌ |
| 16 MS | 133.333 | 149 ❌ |
| 8 MS | 66.667 | 37 🟡 |
| 4 MS | 33.333 | 9 ✅ |
| 1 MS | 8.333 | 0,6 ✅ |

Empfohlene Strategie: Idle=64MS (leise/präzise), Mittel=16MS, Schnell≥500RPM=4MS, Instantan=1MS.
Wechsel nur wenn Motor < ~50 SPS.

### Coast (toff=0 → Freilauf)
Motor bremst nach `toff=0` in <500 ms auf Stillstand (Back-EMF + Cogging + Lagerreibung).
`olb=1` bei Stillstand nach Coast: erwartet, Artefakt der Strommessung.

### Ghost/StealthChop Minimum-Strom
Bei 400 RPM mit TPWMTHRS=0 (erzwungen StealthChop), Strom-Sweep:
- Motor läuft bis 50 mA / cs_actual=0 — durch Massenträgheit
- Für Positionierung unter Last: ≥150 mA empfohlen

---

## 5. Versionshistorie

| Version | Datum | Kernänderungen |
|---------|-------|----------------|
| **v3.5.4** | 2026-03-24 | ⭐ **MEILENSTEIN** — erste stabile Version (0 Stalls, 0–2500 RPM, cs_actual=28). Baseline für alle weiteren Versionen. |
| v3.6.0 | 2026-03-25 | Fix E1: `setAcceleration(30000)` in runSpeedTest; Fix E2: aktives Settle-Warten; Fix E3: Boost-Logik |
| v3.6.1 | 2026-03-25 | PCNT eingebaut → GPIO-Konflikt (System hing) — defekt |
| v3.6.2 | 2026-03-25 | GPIO-Fix: `gpio_set_direction(INPUT_OUTPUT)` nach `pcnt_unit_config()` |
| v3.6.7 | 2026-03-26 | Pins korrigiert (27/26), PCNT-vor-FAS Initialisierungsreihenfolge |
| v3.6.9 | 2026-03-26 | Tacho-RPM, KATAPULT Phase A/B/C, CCW-Timeout 5→12 s |
| v3.6.10 | 2026-03-26 | KATAPULT → Parcour-Toggle; VORFÜHRMODUS; Hunt ab ≥600 RPM; Spinup-Zeit fix |
| v3.6.11 | 2026-03-26 | `real_rpm`-Spalte in Telemetrie; Tacho-Stall-Detektion im Hunt; StealthChop Auto-Switch; Katapult-Finale |
| v3.6.12 | 2026-03-26 | Sensor-Kalibrierung neu (CCW→CW→20sps, Center=0°); `gotoCardinal()`; Tacho-Debounce 5 ms |
| v3.6.13 | 2026-03-26 | Bugfix H1 (NVS-Inkompatibilität), H2 (P1-Race), H3 (TPWMTHRS nach Home) |
| v3.6.14 | 2026-03-26 | Bugfix H4 (alle `stopMove()` ohne `while(isRunning())`); PCNT-ISR in homeMotor; P1 500sps |
| v3.6.15 | 2026-03-26 | Bugfix C2 (characterizeSensor Fahrtrichtungsfehler): A1→A2 CW-Durchfahrt statt CCW+Wende |
| v3.6.16 | 2026-03-26 | characterizeSensor: CCW-Anlauf 0.3 rev vor A1-Suche (reproduzierbarer Startpunkt); A2 auf 500 sps (kein 20sps-Kriechgang) |
| v3.6.17 | 2026-03-26 | homeMotor: Kommentare und Variablen aufgeräumt (`a1_abs` statt `sCW_abs`); characterizeSensor-Logik finalisiert |
| ⭐ **v3.7.0** | 2026-03-26 | **MEILENSTEIN** — Calib + Homing vollständig funktionsfähig. Alle bekannten Bugs (H1–H4, C2) geschlossen. |
| v3.6.19 | 2026-03-26 | Grad-absolutes Koordinatensystem: `CalibrationData` auf `triggerStartDeg`/`triggerEndDeg` (float°) umgestellt. NVS-Versionierung `nvsVersion=3619`. |
| v3.7.1 | 2026-03-27 | Phase 1 Modularisierung (Config/Types/IModule/Sensor/Driver/Telemetry). Driver-Layer Winkel-API (`degToSteps`, `moveToDeg`, …). FreqSweep: kontinuierlicher Chirp 10–200 Hz. `lroundf` für Microstep-Skalierung und Kalibrierungsmitte. |

---

## 6. Bug-Tabelle

| ID | FW | Schwere | Symptom | Ursache | Fix |
|----|----|---------|---------|---------|-----|
| F2 | 3.6.7 | 🔴 | Motor dreht sich nicht | Pin-Swap: 26/27 statt 27/26 angenommen | Pins korrigiert |
| F6 | 3.6.7 | 🔴 | PCNT blockiert RMT | `pcnt_unit_config()` nach FAS überschreibt GPIO-Matrix | PCNT vor FAS initialisieren |
| A1 | 3.6.10 | 🟡 | Phase A stalled bei alle MS @ 200 RPM | SG unter TCOOLTHRS unbrauchbar | Hunt ab ≥600 RPM |
| A2 | 3.6.10 | 🟡 | SG-Threshold zu aggressiv | SG @ Niedrig-RPM prinzipbedingt ~0 | SG nur wenn `cs_actual > 0` |
| C1 | 3.6.10 | 🟡 | Spinup erreicht ~1125 statt 2000 RPM | delay=2000ms + a=30k sps² zu langsam | Spinup-Zeit erhöht |
| E1 | 3.6.0 | 🟡 | Motor nie bei Ziel-RPM | kein `setAcceleration()` in runSpeedTest | `setAcceleration(30000)` |
| E2 | 3.6.0 | 🟡 | Blind-Settle 500ms | feste Wartezeit unabhängig von Rampe | aktives Warten auf `≥95%` Speed |
| H1 | 3.6.13 | 🔴 | homeMotor fährt irgendwohin | Alte NVS-Cal: `triggerCenter≠0`, `triggerStart` absolute statt relativ | `loadCalibration()` invalidiert wenn `triggerCenter≠0` |
| H2 | 3.6.13 | 🟡 | Kalibrierung P1 Race | `move(0.3 rev)` ohne Stopp-Warten nach P1 | `while(isRunning())` + 0.5 rev Abstand |
| H3 | 3.6.13 | 🟡 | TPWMTHRS nach Home falsch | `applyDriverSettings` mit 64MS, dann `setMicrosteps(16)` ohne Neuberechnung | `applyDriverSettings` am Ende von homeMotor |
| H4 | 3.6.14 | 🔴 | homeMotor fährt falsche Richtung | `stopMove()` ohne `while(isRunning())` → `setCurrentPosition()` während Motor noch läuft | Alle `stopMove()` + wait; PCNT-ISR für exakte Sensorposition |
| C2 | 3.6.15 | 🔴 | Kalibrierung findet Sensor nie, 0° falsch, Homing falsch | Falsche Fahrtrichtung nach P1 (siehe Abschnitt 6.1) | A1→A2 CW-Durchfahrt, kein CCW-Start |
| C3 | 3.6.17 | 🟡 | A1-/A2-Fehlerpath: `stopMove()` ohne `while(isRunning())` | Motor läuft nach Fehler-Return noch aus | Bekannt, unkritisch (Funktion bricht ab, Motor decel von selbst) |
| F11| 3.7.1 | 🟡 | UI "Offline" | `sys.log` enthält unmaskierte `\n`, korrumpiert JSON | ✅ `logData.replace('"','\'')` + `\n`/`\r`-Escaping vor JSON-Einbettung |
| F12| 3.7.1 | 🔴 | `Err: A2<=A1` | ISR überschreibt `lastSensorRaw` während Entprellphase | ✅ ISR-Latch: `if (!sensorHit)` — nur erste Flanke nach Reset wird erfasst |
| F13| 3.7.1 | 🟡 | 1-4 Schritte Drift | `pcntStepperBase` Setzen während RMT-Buffer aktiv | ✅ `delay(2)` nach `while(isRunning())` vor PCNT-Reset in homeMotor + characterizeSensor |
| F14| 3.7.1 | 🔴 | Homing verschoben | Microstep-Umschaltung nach `moveToDeg(0)`, ±1 Step Restfehler wird ×4 | ✅ `setPositionDeg(0.0f)` nach `while(isRunning())` in homeMotor, vor `setMicrosteps(64)` |
| C4 | 3.7.1 | 🟡 | Back-Off-Timeout in homeMotor ignoriert | `waitForSensorTimed(HIGH, …)` Rückgabewert ungeprüft → bei Timeout PCNT-Sync falsch, A1-Position verfälscht | Offen — Rückgabewert prüfen, Fehlerpath analog A1-Suche |
| R1 | 3.7.1 | 🔴 | `Err: A2<=A1` | **Doppel-Monitoring:** ISR und Bounce2 kämpfen um `TACHO_PIN`. ISR speichert Jitter, Bounce2 filtert ihn. | ✅ Kein aktiver Konflikt: ISR=Kantenpräzision (PCNT, F12-Latch), Bounce2=Zustandsbestätigung. `lastSensorRaw` ist 5ms vor Bounce2-Return eingefroren. By design korrekt. |
| R2 | 3.7.1 | 🟡 | Zyklus-Verzögerung | **UART-Overhead:** `updateTelemCache` und Steuer-Funktionen fragen TMC-Register redundant ab. | Kein identifizierbarer redundanter Read. `applyDriverSettings` nach MS-Switch notwendig (TPWMTHRS-Neuberechnung). Niedrige Priorität. |
| R3 | 3.7.1 | 🟡 | Anzeige-Jitter | **Berechnungs-Split:** Winkel-Normalisierung findet redundant in FW und UI-JS statt. | Kein Bug: JS drei-Term-Min für 360°/0°-Wrap nötig (z.B. pos=5°, Marker=360°). FW gibt konsistenten 0-360-Bereich. Beide Ebenen notwendig. |
| R4 | 3.7.1 | 🟡 | Datenverlust | **Buffer-Reset:** Standalone-Katapult/FreqSweep riefen `clearTelemetry()` auf und löschten Parcour-Daten. | ✅ `clearTelemetry()` aus `pendingKatapult`- und `pendingFreqSweep`-Pfaden entfernt — nur Parcour-Start löscht. |

---

### 6.1 Bug C2 — Detailanalyse: Kalibrierung und Homing (v3.6.12–3.6.15)

**Symptom:** Kalibrierung hängt oder setzt 0° an die falsche Position. Homing fährt danach nicht auf 0°.

#### Ursache Teil 1: characterizeSensor fährt nach P1 in die falsche Richtung

Der Algorithmus in v3.6.12–3.6.14 sollte die Sensorzone durch eine CW-Durchfahrt vermessen. Die Idee: erst CCW zum Sensor (P1), dann CW zurück als Anlaufstrecke, dann CW-Präzisionsanfahrt (P2) mit PCNT.

Das Problem entstand beim Übergang P1 → P2:

```
Startposition: irgendwo rechts vom Sensor

P1: runBackward() (CCW) ───────────────────→ Sensor ON
    Motor stoppt auf A1 (rechte Kante des Sensors)

    move(+0.5 * stepsPerRev)  ← HIER DER BUG
    +0.5 rev = CW = nach RECHTS, WEG vom Sensor
    Motor steht jetzt 1600 Schritte RECHTS von A1

PCNT reset (Motor ist rechts vom Sensor)

P2: runForward() (CW) ──────────────────────→ fährt weiter RECHTS
    Sensor ist LINKS, wird nie erreicht → Timeout
```

**Warum der `+` statt `−` ein Fehler ist:** P1 fand den Sensor von rechts kommend (CCW). Der Motor steht auf A1, der rechten Eintrittskante. Um eine CW-Anfahrt von links zu ermöglichen, müsste die Anlaufstrecke CCW (`move(-0.5*stepsPerRev)`) erfolgen — nicht CW. Der `+`-Wert schickte den Motor zurück in die Richtung, aus der er kam.

#### Ursache Teil 2: Falsche Cal-Daten → Homing fährt auf 0° an falscher Stelle

Das Homing (homeMotor) funktioniert korrekt, **wenn** `triggerStart` in NVS stimmt. Da die Kalibrierung fehlschlug oder stark ungenaue Werte speicherte, war `triggerStart` falsch. Konkret:

```
Homing-Logik (korrekt in sich):
  1. Langsame CW-Anfahrt → ISR erfasst sCW_abs (exakte Einschaltkante)
  2. setCurrentPosition(triggerStart + overshoot)
  3. moveTo(0) → bewegt Motor um |triggerStart| − overshoot Schritte CW → Mitte

Wenn triggerStart aus defekter Cal z.B. = −800 statt −25:
  → moveTo(0) = 800 − overshoot Schritte CW = viele Umdrehungen in die falsche Richtung
```

#### Fix in v3.6.15

Statt CCW-Suche + Richtungswechsel: direkte CW-Durchfahrt durch den Sensor.

```
P0: Sensor aktiv? → CW bis inaktiv (clean start)
A1: CW 500 sps bis Sensor ON  → PCNT/ISR: sCW
A2: CW  20 sps bis Sensor OFF → PCNT/ISR: eCW
center = sCW + (eCW − sCW) / 2
moveTo(center) → setCurrentPosition(0)
```

Der Fahrtrichtungsfehler ist strukturell unmöglich, da der Motor durchgehend CW fährt. Homing funktioniert korrekt sobald `triggerStart` aus einer gültigen v3.6.15-Kalibrierung stammt.

---

## 7. FreqSweep — Implementierung

### Prinzip: Kontinuierlicher Chirp via FastAccelStepper

Kein externer Frequenzgenerator-Chip oder Timer nötig. FastAccelStepper nutzt intern das **ESP32-RMT-Peripheral** (Hardware-Pulsgenerator) und berechnet Dreieck-Rampen nativ.

```
f(t) = FREQ_MIN + (FREQ_MAX - FREQ_MIN) × (elapsed / tDur)   ← linearer Chirp
amp  = FREQ_ACCEL_MAX / (16 × f²)    ← aus Dreiecksprofil-Physik:
                                         th = 1/(2f),  th = 2×√(amp/a)  →  amp = a / (16f²)
peak_v = √(a × amp)                  ← Spitzengeschwindigkeit
```

Pro Halbschwingung:
```
setAcceleration(FREQ_ACCEL_MAX)   ← maximale Beschleunigung
setSpeedInHz(peak_v)              ← FAS-Obergrenze (cap bei cal.maxRpm)
moveToDeg(±ampDeg)                ← FAS erzeugt Dreieck: Voll-Rampe rauf + runter
while (isRunning()) { ... }       ← warten, dann sofort nächste Halbschwingung
```

### Nutzbare Frequenzbandbreite (theoretisch, @ 64MS / FREQ_ACCEL_MAX=500000 sps²)

| f (Hz) | amp (Steps @ 64MS) | amp (°) | peak_v (sps) |
|--------|-------------------|---------|--------------|
| 10 | 312 | 8.8° | 12 500 |
| 30 | 35 | 1.0° | 4 167 |
| 60 | 8 | 0.23° | 2 000 |
| 100 | 3 | 0.08° | 1 250 |
| 130 | 1.8 → **< 3 → Abbruch** | — | — |

Effektive Obergrenze: **~120–130 Hz** bei FREQ_ACCEL_MAX=500000.

### Konfiguration (Config.h)

```cpp
#define FREQ_MIN_HZ        10.0f    // Sweep-Start
#define FREQ_MAX_HZ       200.0f    // Sweep-Ende (rechnerisch; real ~130 Hz Limit)
#define FREQ_SWEEP_S       30.0f    // Gesamtdauer
#define FREQ_ACCEL_MAX   500000UL   // sps² — höher → höhere nutzbare Frequenz
#define FREQ_AMP_MIN_STEPS    3     // Abbruch wenn Amplitude < 3 Steps
```

### Startbedingung

FreqSweep beginnt immer bei **0° (Sensormitte)**. Voraussetzung: Motor wurde kalibriert und gehomt.
Aufruf nur über Parcour-Toggle FREQ-SWEEP (kein standalone-Button).

### Bekannte Grenzen

- Sinusförmige Bewegung nicht möglich ohne Echtzeit-Geschwindigkeitsnachführung via Hardware-Timer
- Frequenzen > ~130 Hz physikalisch nicht erreichbar (Amplitude < 3 Steps → Abbruch)
- Bei sehr kleinen Amplituden (<5°): Positionsgenauigkeit durch Microstep-Raster begrenzt

---

## 8. Telemetrie-Analyse

### CSV-Format (ab v3.6.11)
`ts_ms, phase, val, pos_steps, spd_sps, real_rpm, sg_result, cs_actual, cur_a, cur_b, stall, otpw, ot, ola, olb`

`real_rpm` = Tacho-RPM aus ISR-Periodenmessung — unabhängig von FAS. Schleppfehler (Motor steht, FAS zählt weiter) sichtbar als `real_rpm=0` bei `spd_sps>0`.

### Lauf parcour_2026-03-26T09-32-34.csv (v3.6.13)
- **HUNT:** 5 MS-Stufen getestet, alle bis 2400 RPM ohne Stall
- **LAUNCH_CCW @ 2400 RPM:** real_rpm stabilisiert sich bei 2727. Keine Stalls. ✅
- **COAST:** real_rpm=4285 konstant während FAS abbremst (Schleppfehler-Artefakt: Motor läuft schneller als FAS-Soll)
- **GHOST/Katapult-Finale:** spd_sps=0, real_rpm 937→750 (Motor costet aus), ola=1 (erwartet). cs_actual 12→0 (Strom-Sweep). ✅

### Bekannte Messprobleme
- `real_rpm=9999`: Tacho-Bounce erzeugt kurze Periode (~6ms). Fix: 5ms Noise-Filter (v3.6.13).
- `real_rpm`-Spikes beim LAUNCH-Anlauf: Tacho settelt sich bei steigender Drehzahl — normal.
- `ola=1` im Ghost/Freilauf: erwartet bei toff=0 oder sehr niedrigem Strom.

### Historische Läufe
| Datei | FW | Ergebnis |
|-------|----|----------|
| parcour_2026-03-25T21-59-29.csv | 3.5.4 | ✅ 0 Stalls, 200–2500 RPM, cs=28 |
| parcour_2026-03-26T06-12-38.csv | 3.6.9 | Phase A alle stalled @ 200 RPM (Bug A1); Phase B 2000 RPM OK |
| parcour_2026-03-26T08-44-52.csv | 3.6.11 | real_rpm=0 in SPEED (Bounce-Bug); Stall @ 2500 RPM sichtbar |
| parcour_2026-03-26T09-32-34.csv | 3.6.13 | LAUNCH @ 2400 RPM sauber; GHOST costet korrekt aus |

---

## 8. Offene Messaufgaben

| # | Test | Zweck |
|---|------|-------|
| T1 | runInertiaTest mit a_max >100k sps² | Reale Stall-Grenze bei extremen Rampen |
| T2 | Microstep-Wechsel 16MS → 4MS während Parcour | Kurzrampen unter 10 Umdrehungen |
| T3 | Belasteter Welle (Gewicht auf Teller) | Minimum-Strom und max. SG unter Last |
| T4 | Kalibrierung + Homing nach v3.7.1 verifizieren | A1→A2 korrekt? 0° landet auf Sensormitte? Parcour startet/endet sauber? |
| T5 | FreqSweep 10–200 Hz am echten Motor messen | Welche Frequenzen sind tatsächlich erreichbar? Wann fällt Amplitude unter Limit? Telemetrie auswerten. |
| T6 | Bug C4 beheben (Back-Off-Timeout homeMotor) | `waitForSensorTimed` Rückgabewert prüfen, bei Timeout sauber abbrechen |

---

## 9. Vorschläge

### S1 — Vollständige Modularisierung
Trotz Aufsplittung in Driver/Sensor/Telemetry ist `MotorControl.cpp` noch ein Monolith (>30KB).
**Ziel:** Verlagerung der Test-Abläufe (Phase 6) und der Kalibrierungs-Logik (Phase 3) in eigene Plugin-Klassen, um `MotorControl` als reine Koordinationsinstanz zu nutzen.

### S2 — Logischer Interlock (State-Machine) ✅
**Implementiert v3.7.1.** `MotorOpState`-Enum in `Types.h` (IDLE/HOMING/CALIBRATING/LEARNING/TESTING/SHOWING). `sys.opState` wird in `TaskCore1` um jeden blockierenden Aufruf gesetzt/gelöscht. `/cmd`-Handler lehnt alle Befehle außer `stop`/`pwr` mit HTTP 409 ab wenn `opState != IDLE`. JS zeigt "BUSY — CMD ignoriert" im Log. `/status` liefert `"op":"homing"` etc. für UI-Anzeige in der Navigationsleiste.

### S3 — UART-Entkopplung ✅
**Bereits implementiert.** `updateTelemCache()` läuft in einem eigenen FreeRTOS-Task auf **Core 0** (`xTaskCreatePinnedToCore(..., 0)`). Motor-Task auf Core 1 unberührt. Kein Handlungsbedarf.

### S4 — NVS Flash-Schutz (Dirty-Bit)
Jeder `saveCalibration` Aufruf schreibt physisch in den Flash.
**Gefahr:** Unnötiger Verschleiß der Flash-Zellen bei häufigen Tests.
**Ziel:** Nur schreiben, wenn sich Daten real geändert haben (Dirty-Bit Vergleich).

### S5 — Dynamischer Tacho-Filter ✅
**Implementiert v3.7.1.** `TACHO_NOISE_FILTER_MS 5` in `Config.h`. ISR nutzt den Wert statt Hardcode. Obergrenze: 60.000 / (5ms × 3200 Steps) ≈ 3.750 RPM messbar bei 16MS; bei 64MS ≈ 937 RPM — ausreichend für aktuelle Anwendung. Anpassbar ohne Code-Änderung.
