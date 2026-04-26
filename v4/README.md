# PerlinNoise v4 — Modulare Motorsteuerung

**Stand:** 2026-04-26 (Phase 0 — Skelett angelegt, kein Code)
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

- **Phase 0** — Layer-Struktur angelegt, FSD geschrieben ✅ aktuell
- **Phase 1** — Skelett: L0+L1+L2+L3+Minimal-L7 lauffähig
- **Phase 2** — Mechanik (L4) + Multi-Motor
- **Phase 3** — Charakterisierungs-Tests (L5a)
- **Phase 4** — Bewegungs-Synthese (L5b)
- **Phase 5** — Watchdog + Profile (L6)
- **Phase 6** — Voll-UI (L7)

## Vorgänger

- `../v3/` — produktive Engineering-Version, läuft auf `perlin-v3.intern.gaengeviertel.de`. Während v4-Entwicklung **unangetastet**.
- `../v4_iteration1/` — erster v4-Versuch (3 Commits, Watchdog + MotorProfile, nicht funktional integriert). Quelle für L6-Komponenten.
- `../src/main.cpp` — Original V1 (read-only), Quelle für L5b und Performance-UI.

## Build

PlatformIO-Env wird in Phase 1 angelegt. Bis dahin: kein Build möglich.
