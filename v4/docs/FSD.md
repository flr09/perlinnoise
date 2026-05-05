# FSD — PerlinNoise v4 (Modular)

**Stand:** 2026-05-05 — Firmware **v4.1.9** auf Hardware. Phase 8 läuft als 7-Tag-Plan v4.1.7 → v4.2.2. v4.1.9 = Persistence Config mit 5 s Debounce gegen NVS-Wear-Out (Bug-ID 23a). Plan in `v4/README.md`.
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

**Hardware fix:** FYSETC E4 (ESP32 + 4× TMC2209) bleibt definitiv. Kein Hardware-Wechsel im v4-Scope.

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

### Core-Verteilung (Designentscheidung in L0)

ESP32 hat zwei Cores. Der WiFi-Stack (lwip, esp-wifi) ist **hardcoded auf Core 0** (Espressif-SDK, nicht abschaltbar). Daraus folgt: damit Movement keine Delays durch WiFi-Hickups bekommt, muss Movement auf **Core 1** (App-CPU) gepinnt werden.

| Core | Tasks | Begründung |
|---|---|---|
| **Core 0** (Pro-CPU) | • WiFi-Stack (hardcoded)<br>• AsyncTCP / WebServer-Handler (`CONFIG_ASYNC_TCP_RUNNING_CORE=0`)<br>• `TelemetryPollTask` (10 Hz, ~10 ms Last)<br>• `WatchdogTask` (50 Hz, ~1 ms Last)<br>• `Logger`-Flush (asynchron) | WiFi-Stack ist hier eh fix. Die zusätzlichen Tasks sind kurz und periodisch — sie blockieren WiFi nicht. |
| **Core 1** (App-CPU) | • `setup()` / `loop()` (Arduino-Default — nur `ArduinoOTA.handle()`)<br>• **`MovementTask`** (FreeRTOS, Priorität 1) — ruft L4-Mechanik + L5-Programme<br>• ISRs für PCNT/Tacho (`attachInterrupt` wird hier aufgerufen → ISR auf Core 1) | Core 1 ist sonst weitgehend leer → Movement bekommt fast 100 % der CPU. |

**Warum nicht andersrum (Movement auf Core 0, WiFi auf Core 1)?** Weil der WiFi-Stack hardcoded auf Core 0 läuft — man kann ihn nicht verschieben. Movement auf Core 0 würde die CPU mit dem WiFi-Stack teilen müssen. Genau das wollen wir vermeiden.

**Zusatzregeln gegen Delays:**
- Sensor-busy-waits in L4: `vTaskDelay(1)` statt `yield()` — gibt aktiv den FreeRTOS-Scheduler frei.
- Critical Sections für globale State-Reads: `portMUX_TYPE` (Spinlock) — sehr kurz, kein Context-Switch.
- TMC2209-UART (1–10 ms pro Kommando): FreeRTOS-Semaphore (`uartMutex`).
- Movement-Task-Priorität auf 1, alle Core-0-Tasks auf 0 — FreeRTOS bevorzugt Movement.

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
| `Prog_CurrentSweepHiRPM` | Strom-Sweep bei festem hohen RPM — sucht „Sweet Spot" wo weniger Strom = mehr Drehzahl (Pseudo-Field-Weakening über B-EMF-Sättigung, **ohne** Microstep-Reduktion) | komplett neu | ❌ |

**Designentscheidung L5a — Microstepping:** v4 läuft durchgehend hochauflösend. Idle/Präzision = 64 MS, kontrolliertes Runterschalten bis 4 MS bei sehr hohen RPM (siehe v3 PROJEKT_DOKU Abschnitt 4). **Kein Fullstep-Modus** (`vhighfs=1`) und **keine Microstep-Tabellen-Manipulation** für Phase Lead — beides würde die Glätte zerstören, die L5b (Bewegungs-Synthese) braucht.

**Field Weakening am TMC2209:** nicht möglich. Der TMC2209 hat kein FOC, keine d/q-Achsen-Steuerung. `Prog_CurrentSweepHiRPM` bildet nur den verwandten **B-EMF-Sättigungs-Effekt** ab: bei sehr hohen RPMs kann *weniger* Strom *mehr* nutzbare Drehzahl bringen, weil das Treiber-Spannungs-Limit weniger stark gegen die Gegen-EMK kämpft. Echtes Field Weakening würde TMC5160 + FOC-Library voraussetzen — Folgeprojekt (Abschnitt 8).

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

### Phase 1 — Skelett (L0 + L1 + L2 + L3 + Minimal-L7) — ✅ abgeschlossen 2026-04-26

**Ziel:** Tragfähiges Architektur-Skelett auf Hardware lauffähig.

**Deliverables:**
- L0–L3 als Datei-Struktur mit Schnittstellen-Headern
- L7 minimal: WiFi (STA+AP), `AsyncWebServer` mit `/` (zeigt nur „v4 Skeleton — Phase 1") und `/status` (FW-Version + Heartbeat)
- 1 Motor-Instanz (X) durchgängig konfigurierbar — Motor X power-on/off via `/cmd?a=pwr`
- Build auf Flashbox, Flash auf perlin-v4-Board (nach OTA-Hostname-Wechsel)

**Tests:**
- T1.1 ✅ Build auf Flashbox erfolgreich (1:39 Min inkrementell, RAM 15 %, Flash 66 %)
- T1.2 ✅ Boot ohne Crash — Serial: `Boot v4.0.0-phase1` → `NVS: ready` → `TMC: X init OK` → `WiFi: STA …`
- T1.3 ✅ WLAN-Verbindung — `perlin-v4.intern.gaengeviertel.de` → `192.168.193.22` (interner DNS, nicht mDNS — siehe Lesson L1.1)
- T1.4 ✅ `/status` JSON antwortet; `/cmd?a=pwr&m=0` toggelt: `m[0].e: false → true ("X: POWER ON") → false`
- T1.5 ✅ Cross-Layer-Check — Includes strikt nach unten, keine Verletzungen

**Lessons Learned:**
- **L1.1 — mDNS vs. interner DNS:** `MDNS.begin(HOSTNAME)` reicht nicht für Auflösung in diesem Netz; der interne DNS-Server registriert per DHCP-Hostname und liefert `*.intern.gaengeviertel.de`. Code lässt MDNS-Begin trotzdem stehen (kostet nichts, hilft in anderen Netzen).
- **L1.2 — `build_src_filter`:** PlatformIO findet Layer-Unterordner unter `v4/src/L*/` rekursiv ohne Anpassung — kein `**`-Glob nötig.
- **L1.3 — Build-Strategie A bestätigt:** PIO komplett auf Flashbox läuft. Erstbuild ~5 Min (Toolchain-Download), inkrementell ~1:30. Pi 400 reicht problemlos.
- **L1.4 — Hard-Reset nach Flash:** ESP32 ist nach ~12 Sekunden voll online (WLAN-Stack braucht ~5 s, dann setup() durch).

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
- T3.3 Prog_CurrentSweepHiRPM findet Sweet-Spot (oder belegt: gibt es nicht)

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
| Q1 | Welche GPIOs für Y/Z/E STEP+DIR auf FYSETC E4? | **geschlossen 2026-04-26:** Y=33/32, Z=14/12, E=16/17 (offizielles FYSETC-E4-README, Hal_Pins.h aktualisiert) |
| Q2 | Welcher Endstop-Pin für X-MIN/Y-MIN/Z-MIN? | **geschlossen 2026-04-26:** X-MIN=34, Y-MIN=35, Z-MIN=15. v3-„TACHO_PIN=15" war faktisch der Z-MIN-Pin. E hat keinen MIN-Pin. |
| Q9 | An welchem Motor-Stecker sitzt aktuell physisch der NEMA14? X (v3-Konvention) oder Z (FYSETC-Konvention, mit Sensor an Z-MIN)? | offen — User klären, entscheidet welche `m`-ID die aktive Test-Achse in Phase 2 ist |
| Q3 | `Storage_Presets` — Speicher-Format (JSON-String in NVS-Blob, oder strukturiert)? | für Phase 6 entscheiden |
| Q4 | `Ui_Performance` — komplette V1-edit.html portieren oder neu? | offen |
| Q5 | LittleFS für HTML auslagern? | nice-to-have, nicht v4.0-Ziel |
| Q6 | OTA-Auth `12345678` beibehalten oder rotieren? | beibehalten für jetzt |
| Q7 | `*_3711.cpp` im perlinnoise-Root — was ist das, integrieren? | offen — User klären |
| Q8 | Field Weakening auf TMC2209? | **geschlossen 2026-04-26:** nicht möglich (kein FOC). Pseudo-Effekt nur via `Prog_CurrentSweepHiRPM`. |

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

### Phase 6+ Lessons Learned (2026-04-29)

- **Tacho-Präzision:** Die Umstellung von `millis()` auf `micros()` in `HalTacho` ist zwingend. Bei 3.000 RPM (20ms/Umdrehung) führt ein Jitter von 1ms (5%) zu Fehlalarmen im Watchdog (Schwellwert 8%). Mit `micros()` sinkt der Messfehler auf <0.01%.
- **Task-Entkopplung:** Die `SynthesisTask` muss zwingend von der `MovementTask` getrennt sein. Blockierende Engineering-Tests (SpeedTest, Homing) in der `MovementTask` ließen zuvor alle anderen Motoren einfrieren. Jetzt läuft die Synthese stabil mit 100Hz auf Core 1 (Prio 2), während Service-Tasks mit Prio 1 laufen.
- **FreqSweep Geometrie:** Ein Schwingen um die Sensor-Mitte führt bei kleinen Amplituden (hohe Frequenzen) zu "0 Pulsen", da der Sensor nie verlassen wird. Der Sweep muss zwingend an der **Sensorkante** (`triggerStartDeg`) oszillieren, um bei jeder Amplitude eine Rückmeldung zu garantieren.
- **Auto-Recovery:** Nach einem Stall im Parcours ist die absolute Position verloren. Ein automatisches `Homing::run()` nach jedem erkannten Stall in den Tests stellt die mechanische Integrität für nachfolgende Tests sicher.
- **OTA-Fallback:** `ArduinoOTA` (Port 3232) erwies sich als instabil bei Netzwerk-Timeouts. `ElegantOTA` (HTTP-basiert via Port 80) wurde als robustes Fallback integriert.

### Phase 4.0.2 Lessons Learned (2026-05-02)

- **µs-Migration vollständig durch:** `Hal_Tacho.cpp`, `MotorProfile.cpp/h` und das JSON-Echo in `/watchdog` ziehen die ms→µs-Umstellung aus 4.0.1 nach. NVS-Layout-Version `MotorProfileNs::NVS_VER` 4001 → **4002** (alte Profile werden invalidiert, automatischer Re-Calib beim ersten Boot nach Update).
- **ElegantOTA produktiv** unter `/update` (Auth `admin/12345678`) — nicht mehr nur Code-Bibliothek, sondern in `WebServer::begin()` registriert.
- **Charakterisierung verifiziert auf Z-Motor (Vorführung 7/7 grün, 2026-05-02 09:04–09:06):** `maxRpm=1500` (Stall bei 1600), `maxAccel=500000`, Sweet-Spot Floor `1000mA` (über Soft-Limit 900mA → Hinweis: bei dauerhaft >900mA thermische Last beobachten), `SG-Thrs=180`, Coast-Pulses 0 → mechanisch sauber. **X/Y/E** noch ohne Sensor-Halterung, Charakterisierung deshalb nur Z.
- **GUI-Bindung kaputt** (Phase 7 startet hier): Drehregler in der Web-UI senden zwar via `/set` an die Engine (`v4::rt`), die Engine reagiert korrekt (Motor bewegt sich) — aber:
  - Slider-`max`-Attribute sind hardcoded statt aus den NVS-Bounds (`maxRpm`/`maxAccel`/`learnedCurrentMA`) abgeleitet. Folge: User kann die Engine über die gemessenen Grenzen drehen.
  - Noise-Canvas in `WebServer.cpp:304-337` rechnet lokal (`off+=.4`) und ignoriert `v4::rt` komplett → kein visuelles Feedback auf Slider-Änderungen.
  - Wellenform-Wechsel (moveType 3–5 Sinus/Sawtooth/Square) ändert den Motor-Output, aber das Canvas zeigt weiter dieselbe Simplex-Animation.
  - `/set`-Endpoint antwortet mit Plain-Text `"OK"` statt JSON-Echo → JS kann Slider-Anzeige nach POST nicht refreshen.
  - Es fehlt ein `/preview`-Endpoint, der Engine-Samples für die Visualisierung ausliefert.

### Phase 7 Plan (Reaktivierung GUI ↔ Engine) — abgeschlossen

| Schritt | Inhalt | Status |
|---|---|---|
| **A** | NVS-Bounds (`CalibrationData`) als `/bounds`-Endpoint, Slider-`max` daran gebunden, Engine-Cap in `Synthesis::start` | ✅ v4.1.0 |
| **B** | `/set`-Endpoint antwortet mit JSON-Echo des `v4::rt`-States; JS refresht Slider aus Echo | ✅ v4.1.1 |
| **C** | `Synthesis::getPreviewBytes()` API; `/preview`-Endpoint liefert Sample-Grid (Noise 32×32 / Wave 128 / w=0); Status-Polling 500 ms → 100 ms | ✅ v4.1.2 |
| **D** | Canvas-JS: lokale Simplex-IIFE durch `setInterval(drawPreview, 100)` ersetzt; Mode-Switch via `j.mode` (Noise / Wave mit Bit7-Phasen-Marker / Idle); `visibilityState`-Guard | ✅ v4.1.3 |

### Phase 4.1.3 Lessons Learned (2026-05-04)

- **Calib-Skip via Step-Counting** (`L4_mechanics/Calibration.cpp`): Bei valider NVS-Cal wird die nach P1+P2 gemessene Zungenbreite (`pos(P2) − pos(P1)`) gegen die gespeicherte verglichen. Toleranz ±5 %. Bei Übereinstimmung entfallen P3+P4 (3-Touch je 3 s), Mitte = (P1+P2)/2. Spart 6–10 s pro Calib bei unveränderter Mechanik.
  - **Bugfix v4.1.3-rc2:** In rc1 wurde p2Pos erst nach `stopMove + waitWhileRunning + delay(80)` erfasst, p1Pos aber direkt beim Trigger. Asymmetrie ~133 Steps Bremsweg (bei 2000 sps + 15000 Decel) → measured ist konstant zu groß → SKIP_TOL=5% greift nie. Fix: p2Pos jetzt in derselben Trigger-Schleife sofort beim `checkStable(HIGH,5)`-Treffer gelesen, vor stopMove.
  - **Logic-Check:** p1Pos und p2Pos durchlaufen denselben `checkStable(…, 5)`-Filter (~5 ms × Geschwindigkeit Latenz). Der Bias ist auf beiden Seiten gleich → bei `p2Pos − p1Pos` hebt er sich auf. Mitte hat einen kleinen Rest-Bias von ~1 °, der für Engineering-Tests irrelevant ist.
  - Diagnose-Log läuft IMMER: `CAL: check d=<measured> exp=<expected> Δ=<abs> (<promille>‰)` — auch im Miss-Pfad sichtbar warum nicht skippte. Marker `CAL_SKIP_OK` / `CAL_SKIP_FAIL` im Telemetrie-Log.
- **FreqSweep v2 ZURÜCKGEROLLT in 4.1.3-rc3** (Bug-IDs 21+22 in `project_perlin_bugs.md`): Hardware-Test 2026-05-05 mit Z-Motor zeigte zwei harte Bugs:
  - **Re-Home-Race:** Bei Stall mit kleiner amp scheiterte `Homing::run()` mit `EdgeTouch miss`, Folgetests an Müll-Position → Bisektion kollabierte.
  - **Hysterese-Floor-Detektor:** `pulses < swings/2` unterscheidet nicht zwischen „Motor stallt" und „Bewegung zu klein für Sensor-Hysterese-Crossing" → Bisektion lief auf `stallAmp=5` (= `FREQ_AMP_MIN`-Floor) bei 5 von 10 Bändern.
  Code zurückgerollt auf den linearen Chirp aus 4.1.2. NVS-Felder `freqStallAmp[10]`/`freqRunCount` in `CalibrationData` bleiben als Reserve (NVS-Schema 4002 unverändert) für eine spätere v2-Iteration mit Stallguard-Cross-Check und vorgelagerter Hysterese-Floor-Bestimmung pro Frequenz.
- **NVS-Schema 4001 → 4002** (`Types.h` + `Storage_Calib.cpp`): Felder `uint16_t freqStallAmp[10]` + `uint8_t freqRunCount` + `_pad[3]` ergänzt. Alte 4001-Daten werden in `load()` als invalid verworfen (size-mismatch + version-mismatch). **Konsequenz:** automatischer Re-Calib beim ersten Boot nach 4.1.3-Update.
- **ElegantOTA-Reboot-Fix** (`main_v4.cpp`): `ElegantOTA.loop()` fehlte im main-loop bis 4.1.2. Folge: Browser-Updates wurden korrekt empfangen und in die Boot-Partition geschrieben (`Update.end(true)`), aber `_reboot`-Flag wurde nie ausgewertet → Board lief mit alter Firmware weiter. Workaround beim 4.1.2 → 4.1.3-Flash war Power-Cycle. Ab 4.1.3 ist `ElegantOTA.loop()` neben `ArduinoOTA.handle()` im main-loop, Auto-Reboot nach 2 s funktioniert.
- **Canvas auf /preview-Polling** (`L7_web/WebServer.cpp` ~Z. 313): Lokale Simplex-Animation komplett entfernt. Drei Render-Pfade: `drawNoise` (32×32 Pixmap → temp-Canvas → 128×128 hochskaliert mit `imageSmoothingEnabled=false`), `drawWave` (128 Bytes als 1D-Polyline, Bit7 markiert Phasen-Start als orange vertikale Linie), `drawIdle` (dunkler Screen mit pulsierendem Hue). Polling 10 Hz, `visibilityState`-Guard.

---

## 10. Revisionshistorie

| Version | Datum | Autor | Änderungen |
|---|---|---|---|
| 4.0.0 | 2026-04-26 | User | Initiale FSD für modulare v4 |
| 4.0.1 | 2026-04-29 | Gemini | Update Phase 6+: micros-Timing, Synthesis-Task, Edge-FreqSweep, Auto-Homing |
| 4.0.2 | 2026-05-02 | Claude | µs-Migration in `.cpp` nachgezogen, NVS-VER 4002, ElegantOTA produktiv, Bounds-Stand Z-Motor dokumentiert, Phase 7 (GUI-Reaktivierung) eröffnet |
| 4.1.0 | 2026-05-02 | Claude | Phase 7A: `/bounds`-Endpoint, Engine-Cap aus NVS-Charakterisierung |
| 4.1.1 | 2026-05-02 | Claude | Phase 7B: `/set` antwortet mit JSON-Echo statt Plain-`OK` |
| 4.1.2 | 2026-05-02 | Claude | Phase 7C: `Synthesis::getPreviewBytes()` + `/preview`-Endpoint, Status-Polling auf 10 Hz hochgezogen |
| 4.1.3 | 2026-05-05 | Claude | Phase 7D: Canvas-JS auf `/preview`-Polling. Calib-Skip-Methodik mit Decel-Bias-Fix (rc2) eingebaut, fällt aktuell wegen Methodik-Mismatch P1+P2 vs. 3-Touch immer auf 3-Touch zurück (Self-Consistency-Fix in 4.1.4). Calib mit 360°-Limit (Phase 0+1). FreqSweep v2 (rc1) wegen Re-Home-Race + Hysterese-Floor zurückgerollt (rc3) — linearer Chirp wieder aktiv. NVS-VER 4002 (Felder `freqStallAmp[10]` bleiben Reserve). ElegantOTA-Reboot-Fix (`ElegantOTA.loop()` im main-loop). |
| 4.1.4 | 2026-05-05 | Claude | Calib-Skip Self-Consistency: NVS 4002 → 4003 mit `uint16_t fastWidthSteps`. 3-Touch-Pfad speichert die in DIESEM Run gemessene P1+P2-Breite als Skip-Referenz für den nächsten Lauf. Verifiziert: Calib-Zeit ~15 s → ~5 s bei Δ=1 Step (5 ‰) in Lauf #2. Hebt Sensor-Hysterese und Methodik-Mismatch auf, weil Referenz und Messung mit derselben Methode entstehen. |
| 4.1.5 | 2026-05-05 | Claude | Bauhaus-Refactor (kein funktionaler Change): NoiseEngine konsolidiert die SimplexNoise-Instanz, `Synthesis::tick()` und `getPreviewBytes()` rufen jetzt `ne.noise(x,y)` statt eigener `static SimplexNoise sn`. Tote ISR-Templates in `Hal_Tacho.cpp` entfernt (1 kHz-Polling-Task ist und bleibt der einzige Detection-Pfad). Ungenutzter `NoiseConfig nc` aus tick() raus. |
| 4.1.6 | 2026-05-05 | Claude | Hardware Fan/Lamp aktiviert (Bug-ID 26). Neuer `HalOutput`-Block (L1) mit PWM-Fan (GPIO 13, LEDC-Channel 4, 5 kHz, 8 Bit) und discrete Lamp (GPIO 2). `Synthesis::tick()` pusht Werte mit Throttle (nur bei Änderung) auch bei stehender Synthese. Erster Commit der Phase-8-Aufteilung — Gemini-Sammel-Commit `8c0f389` (v4.2.0) wegen kritischer Linker-Fehlleitung verworfen (Bug-ID 27). |
| 4.1.7 | 2026-05-05 | Gemini+Claude | EdgeC-Slider in `Synthesis::tick()` aktiviert via `NoiseEngine.applyShape()` public — Motor und Preview teilen identische Shaping-Math (Bug-ID 25). Wave-Modi (3–5) nutzen `cal.maxAccel` × 0.95 mit `HARD_ACCEL_CAP=500000` für harte Square-Sprünge (Bug-ID 24). **rc2** (Claude): Step-Mode-Slider `v4::rt.accelMax` Regression-Fix — war hardcoded 100k, ist wieder User-controllable mit `cal.maxAccel` als Cap nach unten. |
| 4.1.8 | 2026-05-05 | Claude | Homing-Robustheit nach Stall (Bug-ID 28). `Homing::run()` macht jetzt Zwei-Richtungen-Suche: erst CW max 1.5 rev, bei Miss CCW max 1.5 rev. Insgesamt 3 rev Coverage — nach Stall-induzierter Step-Counter-Desync findet die Sensor-Zunge in jeder Drehrichtung. Helper `searchSensorOneDir(motorIdx, pin, dir, maxRev)` extrahiert. |
| 4.1.9 | 2026-05-05 | Claude | Persistence Config mit Debounce (Bug-ID 23a). Neuer L2-Block `Storage_Runtime` (Schema 4200) speichert RuntimeConfig in NVS-Section `rtconf`. `WebServer::begin()` ruft `load(v4::rt)`, `/set`-Handler ruft `touch()`, main-loop ruft `tickFlush()` alle 10 ms — schreibt erst nach **5 s Ruhe** (kein NVS-Wear-Out beim Slider-Drag, vermeidet Bug-ID 27). Hardware-verifiziert: `speed=0.555, fan=128` überleben Reboot. |
| 4.1.7 | 2026-05-05 | Gemini | Phase 8B+C: Wave-Accel Performance (Fix ID 24) + EdgeC Fix (Fix ID 25). `applyEngineCap` nutzt `maxAccel` für Wave-Modi (capped auf 500k). Noise-Shaping konsolidiert in `ne.applyShape`. |
