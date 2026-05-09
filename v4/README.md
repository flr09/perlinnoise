# PerlinNoise v4 — Modulare Motorsteuerung

**Stand:** 2026-05-09 — Firmware **v4.3.3** auf `perlin-v4.intern.gaengeviertel.de` (192.168.193.22). Phasen 1–8 abgeschlossen. **Phase 9** (Dynamics under the hood) läuft, 4 von 5 Schritten fertig (v4.3.0–v4.3.3).
**Branch:** `v4-modular`

## Was ist v4?

Die zusammengeführte Endversion: künstlerische Bewegungs-Synthese aus V1 (Perlin/Simplex/Sinus auf 4 Motoren) **plus** die Engineering-Reife aus v3 (Sensor-Kalibrierung, Telemetrie, Watchdog) — diesmal in **8 sauber getrennten Schichten**, sodass Reparatur in einer Schicht die anderen unangetastet lässt.

Vollständige Spezifikation: [`docs/FSD.md`](docs/FSD.md).

## Layer-Struktur

| Layer | Ordner | Verantwortung |
|---|---|---|
| L0 | `src/L0_platform/` | Boot, FreeRTOS-Tasks, Logger, Sync-Primitives |
| L1 | `src/L1_hal/` | Pin-Map, Tacho-ISR, PCNT, Sensor-Polling |
| L2 | `src/L2_storage/` | NVS-Wrapper für Cal/Profile/Wifi/Presets |
| L3 | `src/L3_driver/` | TMC2209, FastAccelStepper, Units, Motion-API |
| L4 | `src/L4_mechanics/` | Calibration, Homing, EdgeTouch, SetZero |
| L5 | `src/L5_programs/` | Tests + Bewegungs-Synthese (Plugins) |
| L6 | `src/L6_telemetry_safety/` | Telemetry, Watchdog, MotorProfile, OpState |
| L7 | `src/L7_web/` | WiFi, WebServer, HTTP-Endpoints, HTML-UIs |

**Abhängigkeitsregel:** Eine Schicht darf nur darunterliegende Schichten nutzen.

## Entwicklungsstand

- **Phase 0** — Layer-Struktur angelegt, FSD geschrieben ✅
- **Phase 1** — Skelett: L0+L1+L2+L3+Minimal-L7 lauffähig ✅
- **Phase 2** — Mechanik (L4) + Multi-Motor ✅
- **Phase 3** — Charakterisierungs-Tests (L5a) ✅ (Vorführung 7/7 grün, Z-Motor)
- **Phase 4** — Bewegungs-Synthese (L5b) ✅ Engine, Bindung an UI offen
- **Phase 5** — Watchdog + Profile (L6) ✅
- **Phase 6** — Voll-UI (L7) ✅ Layout, GUI-Bindung kaputt ⚠️
- **Phase 7** — GUI-Reaktivierung (Bounds, /set-Echo, /preview, Canvas) ✅ (v4.1.0–4.1.3)
- **v4.1.4** — Calib-Skip Self-Consistency (`fastWidthSteps` in NVS 4003, Calib-Zeit ~15 s → ~5 s) ✅
- **v4.1.5** — Bauhaus-Refactor (NoiseEngine konsolidiert, Hal_Tacho-Toter-Code raus) ✅
- **Phase 8** ✅ v1-Parity + Hardware + Reversal-Charakterisierung (abgeschlossen):
  - **v4.1.6** ✅ Hardware Fan/Lamp HAL (HalOutput, PWM-Fan + discrete Lamp)
  - **v4.1.7** ✅ EdgeC + Wave-Accel + Step-Mode-Slider-Fix (rc2)
  - **v4.1.8** ✅ Homing-Robustheit nach Stall (Bug-ID 28: CW+CCW je 1.5 rev)
  - **v4.1.9** ✅ Persistence Config (Storage_Runtime, 5 s Debounce, ID 23a)
  - **v4.1.10** ✅ Persistence Presets (8 NVS-Slots, ID 23b)
  - **v4.2.0** ✅ WiFi NVS + nicht-blockierender Reboot (ID 23c)
  - **v4.2.1** ✅ Reactive Caps + Cal-Cache (Gemini, ID 24/29 partial/30)
  - **v4.2.2** ✅ FreqSweep v2 retake (Tacho mit feinem 6..50 Hz Raster, IDs 18+21+22 — Sensor-Hysterese-Schwelle bei ~9 Hz auf Z-Mechanik)
  - **v4.2.3** ✅ Synthesis Wave-Cap aus FreqSweep-v2-Daten (ID 29 final)
  - **v4.2.4** ✅ Sinus silent — per-Wave-Mode Acc-Discrimination (ID 34)
- **Phase 9** Dynamics under the hood — TMC-Tuning, 256 µSteps, SG4 (5-Tag-Plan, läuft):
  - **v4.3.0** ✅ `intpol(true)` für 256 µStep-Hardware-Interpolation (Lichtinstallation, glattere Sinus). Eine Zeile in `Tmc::applyDefaults()` — externe FAS-Steps werden vom TMC2209 intern auf 256 µSteps interpoliert.
  - **v4.3.1** ✅ TPWMTHRS-Hybrid pro Mode (`Synthesis::applyChopperMode()`): Sinus/Noise/Linear/Circle/Figure8 → StealthChop immer (silent). Saw/Step → Schwelle bei **500 RPM** (`Units::rpmToTpwmthrs`). Square → SpreadCycle immer (Power für harte Sprünge). Aufruf in `start()` und beim Mode-Switch im `tick()`.
  - **v4.3.2** ✅ FreqSweep v3 Phase A — `runTachoCutoffDiagnostic()` mit 1/f²-amp-Cap. NVS-Schema 4003→4004 (`tachoCutoffHz`). Befund: SG-Spalte durchgehend 0 → Phase B (SG-Fusion) verworfen. [Spezifikation](docs/spec_freqsweep_v3.md).
  - **v4.3.3** ✅ FS-Drift-Fix + Wave-Cap-Refactor (Bug 37, 38). Re-Sync zur Sensor-Kante zwischen jedem Sweep-Band — Hardware-Verifikation: fcutoff Z-Motor **11→31 Hz**, kumulative Drift hatte fcutoff ~3× nach unten verzerrt. `Synthesis::capWaveSpeedFromFreqStallAmp` nutzt jetzt zusätzlich `cal.tachoCutoffHz × 0.9` als Hard-Cap.
  - **v4.3.4** ⚪ Player-Watchdog: Synthesis-Selbstheilung. Berechne pro Mode erwartete Tacho-Pulse-Rate (Wave: 2·f wenn Schwingungs-Amplitude > Sensor-Distanz; Noise: speed·range/sensor_width; Step: 1 pro Sprung). Vergleich gegen tatsächliche Pulse über 3 s gemittelt. Bei Drift > 50 % → `Logger::addLog("PLAYER WD: …")` + neues Pause-State (5 s) + `Homing::run(motorIdx)` (robust dank ID 28) + `Synthesis::resume()` mit gleichen `v4::rt`-Werten. Nur für Player aktiv, nicht für Tests.

Detailliert in [`../AGENT_COORDINATION.md`](../AGENT_COORDINATION.md).

## Vorgänger

- `../v3/` — produktive Engineering-Version, läuft auf `perlin-v3.intern.gaengeviertel.de`. Während v4-Entwicklung **unangetastet**.
- `../v4_iteration1/` — erster v4-Versuch (3 Commits, Watchdog + MotorProfile, nicht funktional integriert). Quelle für L6-Komponenten.
- `../src/main.cpp` — Original V1 (read-only), Quelle für L5b und Performance-UI.

## Build

PlatformIO. Envs in `../platformio.ini`:
- `fysetc_e4_v4` — USB-Build (Flash via Flashbox/`/dev/ttyUSB0`)
- `fysetc_e4_v4_ota` — OTA → `perlin-v4.intern.gaengeviertel.de`
- alternativ: ElegantOTA über die Web-UI (`/update`, Auth `admin/12345678`)
