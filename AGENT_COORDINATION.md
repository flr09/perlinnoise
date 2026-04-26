# 📌 AGENT COORDINATION HUB

## 🕒 Aktueller Status (LIVE)
- **Stand:** 2026-04-26
- **Branch:** `v4-modular`
- **Phase:** v4 Phase 0 — Layer-Skelett angelegt, FSD geschrieben, kein Code
- **Vorgänger v4_iteration1/** liegt zur Seite (3 Commits, baubar via env `fysetc_e4_v4_iter1`)
- **v3** läuft produktiv online unter `perlin-v3.intern.gaengeviertel.de` — bleibt **unangetastet** während v4-Entwicklung

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
| 0 | Skelett + FSD | ✅ aktuell |
| 1 | L0+L1+L2+L3 + Minimal-L7 lauffähig | offen |
| 2 | L4 + Multi-Motor (X/Y/Z mit Sensorik, E open-loop) | offen |
| 3 | L5a Charakterisierung (v3-Tests + 3 Field-Weakening-Tests) | offen |
| 4 | L5b Bewegungs-Synthese (V1-Funktionalität zurück) | offen |
| 5 | L6 Watchdog + Profile (aus v4_iteration1 integrieren) | offen |
| 6 | L7 Voll-UI (Lab + Performance + Telemetry-Viewer) | offen |

## 🛠️ Wichtige Erkenntnisse (Shared Knowledge)

- **E-Motor hat keinen Endstop-Pin** → läuft open-loop, nur `/setzero` als Nullpunkt. Cal/Home nur für X/Y/Z.
- **Sensorik:** LJ12A3-4-Z/BX (induktiv, NPN NO) — AS5600 evtl. später für Folgeprojekt.
- **Hardware-Vorbehalt:** Aktuell nur Z mit montiertem Sensor (mechanisch). Y/X/E warten auf neue Konstruktion.
- **GPIO 15** möglicherweise vorbeschädigt durch PNP-Vorfall am 2026-03-24 — bei Sensor-Problemen im Hinterkopf behalten.
- **Field Weakening** bei TMC2209 nicht direkt möglich (kein FOC). Drei verwandte Test-Programme in L5a geplant: `Prog_FullstepSwitch`, `Prog_PhaseLead`, `Prog_CurrentSweepHiRPM`.
- **WiFi-PW** war in v3 als Klartext in `wifi_settings.h` — in v4 wird das in NVS via `Storage_Wifi` migriert.

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

Vor Phase 1: User-OK zur FSD einholen, GPIOs Y/Z/E STEP+DIR + Endstop-Pins am FYSETC E4 verifizieren (offene Frage Q1+Q2 in FSD).
