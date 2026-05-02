# 📌 AGENT COORDINATION HUB

## 🕒 Aktueller Status (LIVE)
- **Stand:** 2026-05-02
- **Branch:** `v4-modular`
- **Firmware:** **v4.0.2** läuft auf `perlin-v4.intern.gaengeviertel.de` (192.168.193.22)
- **Phase:** v4 Phase 1–6 abgeschlossen ✅ — Charakterisierungs-Suite (L5a) produktiv, L5b Synthese-Engine im Code, **L7 GUI-Reaktivierung beginnt** (Phase A: Bounds aus Testsuite an Slider binden, Phase B: `/set`-Echo, Phase C: `/preview`-Endpoint, Phase D: Wellenform-Visualisierung)
- **Vorgänger v4_iteration1/** liegt zur Seite (3 Commits, baubar via env `fysetc_e4_v4_iter1`)
- **v3 wurde überschrieben** durch v4 (gleiches Board). v3-Quellcode + 35 v3-Bin-Snapshots in Git gesichert. Re-Flash auf v3.7.32 jederzeit möglich via `v3/firmware_v3_3.7.32_20260328_1230.bin`.

## 🎯 Auftrag v4

Modulare Endversion: künstlerische Bewegungs-Synthese aus V1 + Engineering-Reife aus v3, sauber zerlegt in 8 Schichten. Vollständige Spezifikation in [`v4/docs/FSD.md`](v4/docs/FSD.md).

## 🧱 Layer-Struktur

```
L7 Web-API + UI               (HTTP, JSON, HTML)
L6 Telemetrie + Sicherheit    (Watchdog, OpState, EmergencyStop)
L5 Programme (a Char + b Synth)
L4 Mechanik                   (Cal, Home, SetZero)
L3 Treiber                    (TMC, Stepper, Units, Motion)
L2 Persistierung              (NVS)
L1 HAL                        (Pins, ISR, PCNT, Sensor)
L0 Plattform                  (Boot, Tasks, Sync, Log)
```

**Regel:** Eine Schicht darf nur darunterliegende Schichten nutzen.

## 📋 Phasen-Plan (Kurzform)

| # | Inhalt | Status |
|---|---|---|
| 0 | Skelett + FSD | ✅ |
| 1 | L0+L1+L2+L3 + Minimal-L7 lauffähig | ✅ |
| 2 | L4 + Multi-Motor (X/Y/Z mit Sensorik, E open-loop) | ✅ |
| 3 | L5a Charakterisierung (v3-Tests + 3 Field-Weakening-Tests) | ✅ (Vorführung 7/7 grün, Z-Motor) |
| 4 | L5b Bewegungs-Synthese (V1-Funktionalität zurück) | ✅ Engine läuft, UI-Bindung offen |
| 5 | L6 Watchdog + Profile (aus v4_iteration1 integrieren) | ✅ |
| 6 | L7 Voll-UI (Lab + Performance + Telemetry-Viewer) | ✅ Layout, **GUI-Bindung kaputt** ⚠️ |
| **7** | **L7 GUI-Reaktivierung** (Bounds → Slider, /set-Echo, /preview, Wellenform-Canvas) | **aktuell** |

## 🛠️ Wichtige Erkenntnisse (Shared Knowledge)

- **E-Motor hat keinen Endstop-Pin** → läuft open-loop, nur `/setzero` als Nullpunkt. Cal/Home nur für X/Y/Z.
- **Sensorik:** LJ12A3-4-Z/BX (induktiv, NPN NO) — AS5600 evtl. später für Folgeprojekt.
- **Hardware-Vorbehalt:** Aktuell nur Z mit montiertem Sensor (mechanisch). Y/X/E warten auf neue Konstruktion.
- **GPIO 15** möglicherweise vorbeschädigt durch PNP-Vorfall am 2026-03-24 — bei Sensor-Problemen im Hinterkopf behalten.
- **Field Weakening** bei TMC2209 nicht direkt möglich (kein FOC). Drei verwandte Test-Programme in L5a geplant: `Prog_FullstepSwitch`, `Prog_PhaseLead`, `Prog_CurrentSweepHiRPM`.
- **WiFi-PW** war in v3 als Klartext in `wifi_settings.h` — in v4 wird das in NVS via `Storage_Wifi` migriert.

### Lessons aus Phase 6+ → v4.0.2 (April–Mai 2026)

- **Tacho-Präzision µs statt ms** (`Hal_Tacho`, `MotorProfile`): bei hohen RPM (>1000) waren ms-Auflösung und ms-basierte Watchdog-Math zu grob → spurious Stall-Trigger. Migration auf `micros()` + `periodUs`/`lastLowUs`/`NOISE_FILTER_US=5000`. NVS-Profile-Schema von 4001 → **4002** (bricht alte Profile, automatischer Re-Calib beim ersten Boot).
- **Decoupled SynthesisTask** (`L0/L7`): Synthesis und Movement teilten Core 1 — bei Multi-Motor kollidierte das. Synthesis hat jetzt eigenen Task auf Core 1, Movement bleibt davon getrennt.
- **Automated Re-Homing nach Stall** (`L5a`): Speed-Test/CurrentSweep verlieren bei Stall die Home-Position → Test-Suite ruft jetzt nach jedem detektierten Stall automatisch `Homing::run()`, dann weiter. Im Log sichtbar als „Stall erkannt -> Re-Homing…".
- **FreqSweep oszilliert über die Sensorkante** statt linear darüber hinweg: garantiert Tacho-Pulse, auch bei breiten Zungen. `EdgeTouch backOff` 0.05 → 0.15 rev.
- **Cal-Bug 3-fach gefixt** (`L4_mechanics`): P1+P2 nahtlos (kein Reset zwischen den beiden CW-Phasen), EdgeTouch maxDelta-Guard, `LONG_MIN`-Sentinel statt 0 für „noch nicht gesetzt".
- **ElegantOTA** als Web-Updater integriert (`/update`, Auth `admin/12345678`) — Flash via Browser, Flashbox bleibt Fallback.
- **Bounds aus Charakterisierung sind in NVS** (`CalibrationData` in `Types.h:41`): `maxRpm`, `maxAccel`, `learnedCurrentMA`, `sgThrs` — Phase 7 macht diese Werte zu den Slider-Bounds in der GUI (statt hardcoded Maxima).
- **Stand 2026-05-02 Z-Motor:** `maxRpm=1500`, `maxAccel=500000`, `Sweet-Spot Floor=1000mA`, `SG-Thrs=180`. X/Y/E noch ohne Sensor → keine Bounds.

## 🚦 Build-Targets

| Env | Zweck |
|---|---|
| `fysetc_e4_v3` | v3 USB-Build (Referenz) |
| `fysetc_e4_v3_ota` | v3 OTA → `perlin-v3.local` |
| `fysetc_e4_v4` | v4 USB-Build (aktuelle Entwicklung) |
| `fysetc_e4_v4_ota` | v4 OTA → `perlin-v4.local` |
| `fysetc_e4_v4_iter1` | v4_iteration1 — alter Stand vor Modular-Refaktorierung |
| `fysetc_e4_v2` | v2 Bench |
| `fysetc_e4_v2_ota` | v2 OTA → `perlin-bench.local` |

## 📝 Nächste Übergabe

**Phase 2 startet:** L4 Mechanik (Calibration, Homing, EdgeTouch, SetZero, CalibVerify) + Multi-Motor-Erweiterung in L1/L3 (X/Y/Z mit Sensorik, E open-loop).

**Vor Phase-2-Tests am echten Board:** Y/Z/E STEP+DIR und Y-MIN/Z-MIN-Pins am FYSETC E4 verifizieren (FSD Q1+Q2). Aktuelle Annahme im Code (`v4/src/L1_hal/Hal_Pins.h`):
- Y: STEP=33, DIR=32, TACHO=34
- Z: STEP=14, DIR=12, TACHO=39
- E: STEP=16, DIR=17, TACHO=0xFF (kein Endstop)
