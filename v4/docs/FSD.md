# FSD — PerlinNoise v4 (Modular)

**Stand:** 2026-04-26
**Branch:** `v4-modular`
**Vorgänger:** `v4_iteration1/` (3 Commits, Watchdog + MotorProfile, nicht funktional integriert)
**Referenz-Implementierung:** `v3/` (v3.7.32, online unter `perlin-v3.intern.gaengeviertel.de`)

---

## 1. Ziel und Scope

v4 ist die **stabile, modular-wartbare Endversion** der PerlinNoise-Motorsteuerung. Sie bringt zwei Welten zusammen, die in v1/v3 nie gleichzeitig fertig waren:

1. Die **künstlerische Vision aus `beschreibung.txt` (V1, 2026-02-05):**
   4 linear angeordnete Stepper, organische Bewegungen aus Simplex/Perlin-Noise und Wellenform-Synthese, Web-UI mit Live-Parametern.
2. Die **Engineering-Reife aus v3 (2026-03-28):**
   Sensor-Kalibrierung (3-Touch, EMI-resistent), Telemetrie, State-Machine-Interlock, Watchdog-Konzept, charakterisierte Motor-Grenzen.

**Scope-Grenze:** v4 ist explizit **kein Frischanfang** und **kein „v3 + ein bisschen mehr"**. v4 ist eine **Umstrukturierung** des Codes nach 8 klar getrennten Schichten, sodass:
- Reparatur in einer Schicht die anderen unangetastet lässt (Servicability).
- Hardware-Tausch (z. B. AS5600 statt LJ12A3 später) als Plugin in einer einzigen Schicht passiert.
- Multi-Motor-Unterstützung sauber realisierbar ist.

**Außerhalb des v4-Scopes:**
- AS5600-Adapter (Folgeprojekt)
- TMC5160 / FOC / echtes Field Weakening (Folgeprojekt — siehe Abschnitt 8)
- LittleFS-Auslagerung der HTML (möglich, aber nicht Pflicht für v4.0)

---

## 2. Hardware-Vorbehalte (verbindlich)

### Motoren

| Motor | UART-Adresse | STEP/DIR | Sensor-Pin | Sensor verfügbar? |
|---|---|---|---|---|
| X | 1 | GPIO 27 / 26 | X-MIN (GPIO 15) | ✅ |
| Y | 3 | (FYSETC E4 Y-STEP/DIR) | Y-MIN | ✅ |
| Z | 0 | (FYSETC E4 Z-STEP/DIR) | Z-MIN | ✅ |
| E | 2 | (FYSETC E4 E-STEP/DIR) | **kein Endstop** | ❌ |

→ E läuft **open-loop**. Kein Auto-Calibrate, kein Auto-Homing für E. Nullpunkt nur via `/setzero` (manuell, wie in V1). Bewegungs-Synthese (L5b) funktioniert auf allen 4 Motoren, sobald Nullpunkt steht.

→ **Hinweis Mechanik:** Aktuell hat nur Z einen einfach anschließbaren Sensor-Steckplatz an der bestehenden Konstruktion. Eine neue 4-Motor-Konstruktion mit allen 4 Sensor-Halterungen ist geplant — bis dahin laufen Y/X/E ohne Sensorik in der Praxis.

### Sensorik

- Initial: **LJ12A3-4-Z/BX** (induktiv, NPN NO, 3,3 V-tolerant via interner Pull-up)
- Bekannte Risiken: **EMI** durch TMC2209-Switching → 5-Hit-Filter Pflicht (übernommen aus v3)
- **GPIO 15 möglicherweise vorbeschädigt** (PNP-Vorfall am 2026-03-24 — siehe `v3/PROJEKT_DOKU.md` Abschnitt 1)
- Vorbereitung für **AS5600** (magnetisch, I²C) als Folgeprojekt: `Hal_Tacho`-Block hat Interface, das ein AS5600-Adapter füllen kann ohne L4/L5 anzufassen

### Motor (alle 4 identisch — Stand jetzt)

- 36BYG1204-A-6QHT (NEMA14 Pancake, Variante A, 0,92 A RMS Nenn)
- **Soft-Limit:** 900 mA, **Empfohlen:** 650–850 mA
- siehe `motors/pancake/datasheet.md` für vollständige Daten

---

## 3. Architektur — 8 Schichten

Schichten enthalten **Blöcke**. Ein Block = eine Datei (oder ein eng zusammenhängendes Datei-Paar `.cpp/.h`) mit klarer Schnittstelle. Reparatur eines Blocks ändert die anderen Blöcke nicht.

### Layer-Übersicht

```
┌──────────────────────────────────────────────────────┐
│ L7 — Web-API + UI                                    │  HTTP, JSON, HTML
├──────────────────────────────────────────────────────┤
│ L6 — Telemetrie + Sicherheit                         │  Watchdog, OpState, ES
├──────────────────────────────────────────────────────┤
│ L5 — Programme (Plugins via IModule)                 │
│   L5a Charakterisierung   L5b Bewegungs-Synthese     │
├──────────────────────────────────────────────────────┤
│ L4 — Mechanik                                        │  Cal, Home, SetZero
├──────────────────────────────────────────────────────┤
│ L3 — Treiber                                         │  TMC, Stepper, Units
├──────────────────────────────────────────────────────┤
│ L2 — Persistierung                                   │  NVS-Storage
├──────────────────────────────────────────────────────┤
│ L1 — HAL                                             │  Pins, ISR, PCNT, Sensor
├──────────────────────────────────────────────────────┤
│ L0 — Plattform                                       │  Boot, Tasks, Sync, Log
└──────────────────────────────────────────────────────┘
```

### Abhängigkeitsregel

**Eine Schicht darf nur auf darunterliegende Schichten zugreifen.** L4 darf L0–L3 nutzen, aber niemals L5–L7. Querverbindungen innerhalb einer Schicht sind erlaubt (z. B. L5a-Block kann anderen L5-Block aufrufen).

---

## 4. Block-Liste

Status-Legende:
- ✅ aus v3 produktiv übernehmbar
- 🔧 in v3 vorhanden, muss zerlegt/refaktoriert werden
- 🆕 in `v4_iteration1` vorhanden, muss integriert werden
- 🎨 war in V1 vorhanden, muss zurückgeholt werden
- ❌ fehlt komplett, muss neu

### L0 — Plattform

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Platform` | `setup()`, `loop()`, FreeRTOS-Tasks (Core 0/1), Serial.begin | v3 main_v3.cpp | 🔧 |
| `Logger` | `addLog`, sys.log mit Trim, JSON-Maskierung | v3 MotorControl.cpp | 🔧 |
| `Sync` | `motorMux` (portMUX), `uartMutex` (Semaphore) | v3 MotorControl.cpp | 🔧 |

### L1 — HAL

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Hal_Pins` | Pin-Map für 4 Motoren (STEP, DIR, ENABLE, TACHO) | v3 Config.h erweitern | 🔧 |
| `Hal_Tacho` | ISR + Periode + pulseCount, **pro Motor instanziiert** (nur X/Y/Z, nicht E) | v3 Sensor.cpp | 🔧 |
| `Hal_Pcnt` | PCNT-Setup, `pcntStepperBase`, IO_MUX-FUN_IE-Tweak (alle 4 Motoren) | v3 Sensor.cpp | 🔧 |
| `Hal_Sensor` | `digitalRead`-Wrapper, `checkSensorStable` (5-Hit), `waitForSensorStable` | v3 Sensor.cpp | 🔧 |

### L2 — Persistierung

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Storage` | NVS-Wrapper mit Schema-Versionierung | v3 MotorControl.cpp | 🔧 |
| `Storage_Calib` | `CalibrationData[3]` (X/Y/Z, kein E) | v3 | 🔧 |
| `Storage_Profile` | `MotorProfile[4]` lesen/schreiben | v4_iteration1 | 🆕 |
| `Storage_Wifi` | WiFi-Credentials in NVS (löst v3-C1-Bug) | komplett neu | ❌ |
| `Storage_Presets` | 8 Preset-Slots (war in V1 nur Browser-LocalStorage) — Server-seitig | offen | 🎨 |
| `Storage_RuntimeConfig` | Live-Parameter (speed, angle, range, frame, cont, …) persistieren | komplett neu | ❌ |

### L3 — Treiber

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Tmc2209` | Pro Motor: rms_current, microsteps, SGTHRS, TPWMTHRS, … (UART-Mutex-geschützt) | v3 Driver.cpp | 🔧 |
| `Stepper` | Pro Motor: FastAccelStepper-Wrapper, Microstep-Switch mit Position-Skalierung, Power-Toggle | v3 Driver.cpp | 🔧 |
| `Units` | `rpmToSps`, `spsToRpm`, `rpmToTpwmthrs`, `degToSteps`, `stepsToDeg` | v3 Driver.cpp | 🔧 |
| `Motion` | `moveToDeg`, `moveByDeg`, `setPositionDeg`, `getPositionDeg`, `waitWhileRunning` | v3 Driver.cpp + Programs.cpp | 🔧 |

### L4 — Mechanik

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Calibration` | 3-Touch Sensor-Kalibrierung — **pro X/Y/Z** | v3 SensorCalib.cpp | ✅ |
| `Homing` | Homing — **pro X/Y/Z** | v3 SensorCalib.cpp | ✅ |
| `EdgeTouch` | `touchSensorEdge` als wiederverwendbare Primitive | v3 SensorCalib.cpp | ✅ |
| `SetZero` | „Aktuelle Position = 0°" für **alle 4 Motoren** | v3 + V1 | 🔧 |
| `CalibVerify` | `characterizeSensorTest` — Diagnose ohne NVS-Save | v3 CalibTest.cpp (in v4_iter1 entfernt) | 🔧 |

### L5 — Programme

#### L5a — Charakterisierung

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Prog_SgLearn` | StallGuard-Threshold lernen | v3 Programs.cpp | ✅ |
| `Prog_SpeedTest` | RPM-Rampe bis Stall | v3 Programs.cpp | ✅ |
| `Prog_InertiaTest` | Beschleunigung erhöhen bis Stall | v3 Programs.cpp | ✅ |
| `Prog_CoastTest` | Auslauf-Pulse messen | v3 Programs.cpp | ✅ |
| `Prog_Katapult` | 3× Anlauf bei maxRpm | v3 Programs.cpp | ✅ |
| `Prog_FreqSweep` | Linearer Chirp 10–200 Hz | v3 Programs.cpp | ✅ |
| `Prog_PerformanceShow` | Speed + FreqSweep nacheinander | v3 Programs.cpp | ✅ |
| `Prog_ProfileLearn` | MotorProfile (RPM↔Periode-Bins) lernen | erweiterbar mit v4_iter1 | 🆕 |
| `Prog_FullstepSwitch` | TMC2209 `vhighfs=1` ab THIGH-Schwelle: verschiebt RPM-Limit nach oben? | komplett neu | ❌ |
| `Prog_PhaseLead` | Microstep-Voreilung bei hohen RPM (B-EMF-Kompensation) | komplett neu | ❌ |
| `Prog_CurrentSweepHiRPM` | Strom-Sweep bei festem hohen RPM — sucht „Sweet Spot" wo weniger Strom = mehr Drehzahl | komplett neu | ❌ |

#### L5b — Bewegungs-Synthese (V1-Funktionalität)

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Synth_Sinus` | Sinus, Frequenz × Phasenversatz pro Motor | V1 main.cpp | 🎨 |
| `Synth_Sawtooth` | Sägezahn mit Formfaktor | V1 main.cpp | 🎨 |
| `Synth_Square` | Rechteck mit Duty-Cycle und Flankenschärfe | V1 main.cpp | 🎨 |
| `Synth_NoiseLinear` | Simplex-Noise linear (Flugrichtung) | V1 + NoiseEngine.h | 🎨 |
| `Synth_NoiseCircle` | Simplex-Noise Kreis | V1 + NoiseEngine.h | 🎨 |
| `Synth_NoiseFigure8` | Simplex-Noise Lissajous | V1 + NoiseEngine.h | 🎨 |
| `Synth_DriveDynamics` | Profil-Skalierung LANGSAM/NORMAL/RASANT | V1 main.cpp | 🎨 |
| `Synth_Pipeline` | Pfadgenerator → Sampling pro Motor (Offset) → Shaping → Range-Skalierung | V1 main.cpp loop() | 🎨 |

### L6 — Telemetrie + Sicherheit

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Telemetry_Poll` | Background-Task Core 0, 10 Hz, TMC-Register cachen | v3 Telemetry.cpp | ✅ |
| `Telemetry_Buffer` | 52 KB CSV-Ringpuffer, `recordDataPoint` | v3 Telemetry.cpp | ✅ |
| `Watchdog` | 3-Beweis-Logik, Self-Arm, 50 ms Task auf Core 0 | v4_iteration1 | 🆕 |
| `MotorProfile` | RPM↔Periode mit linearer Interpolation | v4_iteration1 | 🆕 |
| `OpState` | State-Machine, alle `pending*`-Flags zentralisiert | v3 Types.h + main | 🔧 |
| `EmergencyStop` | `pendingStop` als zentraler Kanal, in alle Blocking-Loops eingebaut (v3-W3-Fix) | v3 + W3-Fix | 🔧 |

### L7 — Web-API + UI

| Block | Verantwortung | Quelle | Status |
|---|---|---|---|
| `Wifi` | STA + AP-Fallback, mDNS, ArduinoOTA, ElegantOTA | v3 main_v3.cpp | 🔧 |
| `WebServer` | AsyncWebServer + CORS + Routen-Registrierung | v3 main_v3.cpp | 🔧 |
| `Api_Status` | `/status` JSON | v3 | 🔧 |
| `Api_Cmd` | `/cmd?a=…` mit HTTP-409-Interlock | v3 | 🔧 |
| `Api_Set` | `/set?param=wert` (Live-Parameter) | V1 | 🎨 |
| `Api_Config` | `/config` (alle Parameter als JSON) | V1 | 🎨 |
| `Api_Setzero` | `/setzero` für alle 4 Motoren | V1 + L4 | 🎨 |
| `Api_Wifi` | `/install`, `/wifisave`, `/wifi-reset` | V1 + L2 | 🎨 |
| `Api_Telemetry` | `/telemetry` (CSV-Download) | v3 | ✅ |
| `Api_Watchdog` | `/watchdog` JSON | v4_iteration1 | 🆕 |
| `Ui_Lab` | Engineering-UI (Buttons, Test-Toggles, Log) | v3 main_v3.cpp index_html | ✅ |
| `Ui_Performance` | Künstlerische UI (Slider, Pattern-Dropdown, Preset-Slots, Motoranzeigen) | V1 index.html / edit.html | 🎨 |
| `Ui_Telemetry_Viewer` | Live-Follow + CSV-Auswertung | v3 telemetry_viewer.html | ✅ |

---

## 5. Phasen-Plan

Jede Phase hat klare **Deliverables** und **Testkriterien**. Nicht zur nächsten Phase, bevor die aktuelle abgeschlossen und getestet ist.

### Phase 1 — Skelett (L0 + L1 + L2 + L3 + Minimal-L7)

**Ziel:** Tragfähiges Architektur-Skelett auf Hardware lauffähig.

**Deliverables:**
- L0–L3 als Datei-Struktur mit Schnittstellen-Headern
- L7 minimal: WiFi (STA+AP), `AsyncWebServer` mit `/` (zeigt nur „v4 Skeleton — Phase 1") und `/status` (FW-Version + Heartbeat)
- 1 Motor-Instanz (X) durchgängig konfigurierbar — Motor X power-on/off via `/cmd?a=pwr`
- Build auf Flashbox, Flash auf perlin-v4-Board (nach OTA-Hostname-Wechsel)

**Tests:**
- T1.1 Build auf Flashbox erfolgreich
- T1.2 Boot ohne Crash (Serial Monitor zeigt FW-Version)
- T1.3 WLAN-Verbindung in `gaengeviertel`-Netz, mDNS `perlin-v4` aufgelöst
- T1.4 `/status` antwortet, `/cmd?a=pwr&m=0` toggelt Motor X ENABLE-Pin
- T1.5 Code-Review: keine Cross-Layer-Verletzungen (z. B. L1 ruft kein L4 auf)

### Phase 2 — Mechanik + Multi-Motor (L4 + Multi-Instanzen in L1/L3)

**Ziel:** Alle 4 Motoren ansprechbar, X/Y/Z kalibrier- und home-bar.

**Deliverables:**
- 4 Motor-Instanzen in L1/L3
- L4: Calibration + Homing für X/Y/Z (3-Touch aus v3)
- L4: SetZero für alle 4
- E-Motor: open-loop, klare Fehlermeldung bei Cal/Home-Versuch

**Tests:**
- T2.1 X kalibrieren, Werte in NVS (Schema 3619), Reboot, Werte erhalten
- T2.2 X homen, fährt zuverlässig auf 0°
- T2.3 SetZero auf E setzt Position=0 ohne Sensor
- T2.4 `/cmd?a=cal&m=3` (E) liefert klare Fehlermeldung
- T2.5 (sobald Hardware verfügbar) Y und Z analog

### Phase 3 — Charakterisierung (L5a)

**Ziel:** Alle Engineering-Tests aus v3 portiert, plus 3 neue Field-Weakening-Tests.

**Deliverables:**
- L5a-Blöcke alle implementiert + via `IModule`-Interface registriert
- `/cmd?a=test&prog=…` startet beliebigen L5a-Block
- Telemetrie-CSV erfasst Phase-Spalte = Block-Name

**Tests:**
- T3.1 SpeedTest auf X erreicht ≥ 2300 RPM ohne Stall (v3-Baseline halten)
- T3.2 FreqSweep läuft 10–~130 Hz wie in v3
- T3.3 Prog_FullstepSwitch zeigt RPM-Verschiebung > 0
- T3.4 Prog_CurrentSweepHiRPM findet Sweet-Spot (oder belegt: gibt es nicht)

### Phase 4 — Bewegungs-Synthese (L5b)

**Ziel:** Künstlerische Vision aus `beschreibung.txt` (V1) wieder lauffähig, jetzt auf v4-Architektur.

**Deliverables:**
- L5b-Blöcke implementiert (Sinus/Sawtooth/Square + Noise-Linear/Circle/Figure8 + DriveDynamics)
- L5b-`Synth_Pipeline`: Pfadgenerator → Sampling pro Motor → Shaping → Range-Skalierung
- L7: `Api_Set`, `Api_Config`

**Tests:**
- T4.1 `/set?run=1&type=0&speed=0.12&angle=45` startet Linear-Noise auf allen 4
- T4.2 V1-Reproduktion: gleiche Slider-Werte → optisch gleiche Bewegung wie V1
- T4.3 Live-Parameteränderung ohne Stop/Restart

### Phase 5 — Sicherheit + Profile (L6)

**Ziel:** Watchdog scharf, MotorProfile lernbar und persistent.

**Deliverables:**
- Watchdog aus v4_iteration1 in L6 integriert, ISR-Hook in L1-Tacho
- MotorProfile aus v4_iteration1 in L6 integriert
- Prog_ProfileLearn (L5a) füllt MotorProfile mit Stützpunkten

**Tests:**
- T5.1 Watchdog schärft sich nach 5 stabilen Revs
- T5.2 Manuell ausgelöster Stall → Watchdog setzt `pendingStop`, Motor stoppt
- T5.3 MotorProfile in NVS, nach Reboot wieder geladen

### Phase 6 — Voll-UI (L7)

**Ziel:** Beide UIs (Lab + Performance) erreichbar, polished.

**Deliverables:**
- L7: `Ui_Lab` (Engineering, aus v3 portiert)
- L7: `Ui_Performance` (Künstlerisch, aus V1 portiert mit edit.html-Funktionalität)
- L7: `Ui_Telemetry_Viewer` (aus v3)
- L7: `Api_Wifi` (Setup-Page, Reset)
- L7: Preset-System (Server-seitig in NVS, ersetzt V1 LocalStorage)

**Tests:**
- T6.1 Lab-UI alle Buttons funktional
- T6.2 Performance-UI startet Synth_NoiseLinear auf 4 Motoren
- T6.3 Preset-Slot speichert/lädt aus NVS
- T6.4 Telemetry-Viewer zeigt Live-Daten

---

## 6. Pfad-Konventionen

```
v4/
├── docs/
│   ├── FSD.md                  ← dieses Dokument
│   └── ARCHITECTURE.md         ← Layer-Diagramme + Schnittstellen
├── src/
│   ├── L0_platform/            ← Boot, Tasks, Logger, Sync
│   ├── L1_hal/                 ← Pins, Tacho, Pcnt, Sensor
│   ├── L2_storage/             ← NVS-Wrapper
│   ├── L3_driver/              ← Tmc2209, Stepper, Units, Motion
│   ├── L4_mechanics/           ← Calibration, Homing, EdgeTouch, SetZero, CalibVerify
│   ├── L5_programs/
│   │   ├── characterization/   ← L5a — Engineering-Tests
│   │   └── synthesis/          ← L5b — Bewegungs-Synthese
│   ├── L6_telemetry_safety/    ← Telemetry, Watchdog, MotorProfile, OpState, EmergencyStop
│   ├── L7_web/                 ← Wifi, WebServer, Api_*, Ui_*
│   └── main_v4.cpp             ← nur setup() + loop()
├── bins/                       ← gebaute Firmware (gitignored ab Phase 1)
└── tele/                       ← aufgezeichnete Telemetrie-CSV
```

`v4_iteration1/` bleibt als Quelle für Watchdog + MotorProfile, wird aber nicht weiterentwickelt.

`platformio.ini` bekommt zusätzlichen `env:fysetc_e4_v4_iter1`, der auf `v4_iteration1/src/` zeigt — damit der alte Stand weiterhin baubar bleibt zum Vergleich.

---

## 7. Offene Fragen / Annahmen

| # | Frage / Annahme | Status |
|---|---|---|
| Q1 | Welche GPIOs für Y/Z/E STEP+DIR auf FYSETC E4? | zu prüfen — Schaltplan-Lookup nötig vor Phase 2 |
| Q2 | Welcher Endstop-Pin für Y-MIN, Z-MIN? | zu prüfen — wahrscheinlich GPIO 35/34 |
| Q3 | `Storage_Presets` — Speicher-Format (JSON-String in NVS-Blob, oder strukturiert)? | für Phase 6 entscheiden |
| Q4 | `Ui_Performance` — komplette V1-edit.html portieren oder neu? | offen |
| Q5 | LittleFS für HTML auslagern? | nice-to-have, nicht v4.0-Ziel |
| Q6 | OTA-Auth `12345678` beibehalten oder rotieren? | beibehalten für jetzt |
| Q7 | `*_3711.cpp` im perlinnoise-Root — was ist das, integrieren? | offen — User klären |

**Annahme A1:** v3 bleibt während v4-Entwicklung unangetastet und online. Bei Bedarf kann jederzeit auf v3 zurückgewechselt werden (Git + Bin-Files).

**Annahme A2:** Flashbox (Pi 400) wird primär für USB-Flashing + serielles Debugging genutzt. OTA-Update via WLAN bleibt wie in v3.

**Annahme A3:** Multi-Motor-Hardware (4 Sensoren montiert) ist erst nach neuer 3D-Druck-Konstruktion verfügbar. Phase 2 wird zunächst nur an Motor X (X-MIN-Pin) verifiziert, Y/Z/E-Pfade per Code-Review geprüft.

---

## 8. Folgeprojekte (außerhalb v4-Scope)

- **AS5600**-Adapter als alternativer `Hal_Tacho` — magnetischer Encoder, I²C, höhere Auflösung als 1-Tick-pro-Umdrehung
- **TMC5160 + FOC** statt TMC2209 + StealthChop — würde echtes Field Weakening, höhere RPM und feinere Drehmoment-Kontrolle bringen
- **LittleFS**-Auslagerung der HTML — kürzere Build-Zeit, einfachere UI-Updates
- **Hardware-Watchdog** auf ESP32-Ebene (zusätzlich zur Software-WD)

---

## 9. Lebendes Dokument

Diese FSD wird gepflegt während der Implementierung. Verworfene Ansätze werden hier dokumentiert (was, warum verworfen, was stattdessen). Pro Phase wird der entsprechende Abschnitt am Ende mit „Lessons learned" ergänzt.
