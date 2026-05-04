# PerlinNoise v4 — Modulare Motorsteuerung

**Stand:** 2026-05-04 — Firmware **v4.1.3** auf `perlin-v4.intern.gaengeviertel.de` (192.168.193.22), Phasen 1–6 abgeschlossen, **Phase 7 (GUI-Reaktivierung) abgeschlossen** (A: Bounds, B: /set-Echo, C: /preview, D: Canvas auf /preview-Polling). Plus zwei Backlog-Punkte mitgenommen: Calib-Skip via Zungenbreite (3-Touch entfällt bei unveränderter Mechanik) und FreqSweep-Redesign (10 Bänder, Bisektion bis Stall, Learning).
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
- **Backlog mitgenommen in 4.1.3:** Calib-Skip via Zungenbreite, FreqSweep v2 (10 Bänder, Bisektion + Learning), ElegantOTA-Reboot-Fix

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
