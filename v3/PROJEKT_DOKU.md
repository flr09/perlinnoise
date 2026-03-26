# PerlinNoise v3 — Projektdokumentation

**Hardware:** FYSETC E4 · ESP32 · TMC2209 · NEMA17 (36BYG1204-A-6QHT, Pancake)
**Sensor:** NPN-Hallsensor an GPIO 15 (TACHO_PIN), Pull-up intern
**Stand:** 2026-03-26 | aktuell: v3.6.14

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

### Hardware-Vorfälle
**2026-03-24 — PNP statt NPN am TACHO_PIN:**
Ein PNP-Schalter wurde irrtümlich an GPIO 15 betrieben. PNP schaltet Versorgungsspannung auf den Pin — ESP32 ist nur 3,3V-tolerant. Risiko: GPIO 15 intern beschädigt. Inzwischen durch korrekten NPN-Schalter ersetzt. Validierung: bei Magnetnähe muss Web-UI "HIT" zwischen TRUE/FALSE wechseln.

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

### Koordinatensystem (ab v3.6.12)
```
  CCW ←──────────────────────────────── CW
        triggerStart      triggerEnd
             │                 │
  ───────────┼──── SENSOR ─────┼────────
          sCW (ON)         eCW (OFF)
                    ↑ CENTER = 0° ↑
                triggerCenter = 0
```
- `triggerStart` = `sCW − center` (negativ, z. B. −25 steps)
- `triggerCenter` = 0 (per Definition)
- `triggerEnd` = `eCW − center` (positiv, z. B. +25 steps)

### Kalibrierung (characterizeSensor)
1. **P1:** CCW 500 sps bis Sensor LOW — sicherstellen wir sind links
2. 0,5 rev CW Anlaufstrecke (clear Sensorzone für P2)
3. PCNT reset — `pcntStepperBase` setzen
4. **P2:** CW 200 sps bis Sensor LOW → ISR erfasst `sCW`
5. **P3:** Weiter CW 20 sps bis Sensor HIGH → ISR erfasst `eCW`
6. `center = (sCW + eCW) / 2` → `moveTo(center)` → `setCurrentPosition(0)`

### Homing (homeMotor)
1. CW 1200 sps bis Sensor LOW (Schnellsuche)
2. `stopMove()` + `while(isRunning())` — vollständig stoppen
3. CCW 400 sps bis Sensor HIGH (Sensor verlassen)
4. `stopMove()` + `while(isRunning())`
5. PCNT reset — `pcntStepperBase` setzen
6. CW 200 sps bis Sensor LOW → ISR erfasst `sCW_abs`
7. `stopMove()` + `while(isRunning())`
8. `setCurrentPosition(triggerStart + (curPos − sCW_abs))` → `moveTo(0)` → Sensormitte = 0°

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
| **v3.6.14** | 2026-03-26 | Bugfix H4 (alle `stopMove()` ohne `while(isRunning())`); PCNT-ISR in homeMotor; P1 500sps |

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

---

## 7. Telemetrie-Analyse

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
| T4 | homeMotor nach v3.6.14 verifizieren | PCNT-basiertes Homing korrekt? |
