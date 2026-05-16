# 🔴 Project PerlinNoise v4 — Bug Log

Legend: 🔴 bug | 🟡 minor/refactor | ✅ fixed | 🔵 discovery | ⏳ awaiting verification

Quellen: dieser Eintrag · `AGENT_COORDINATION.md` Lessons · `v4/docs/FSD.md` §9.

| ID | Date | Type | Title | Status |
|---|---|---|---|---|
| 1 | 2026-05-04 | 🔴 | Calibration-Skip Logic Error (Decel-Bias) | ✅ v4.1.4 |
| 2 | 2026-05-02 | 🔴 | ElegantOTA Reboot missing loop() | ✅ v4.1.3 |
| 3 | 2026-05-04 | 🟡 | NoiseEngine Redundancy | ✅ v4.1.5 |
| 13 | 2026-04-29 | 🔴 | ArduinoOTA instabil | ✅ v4.0.2 (ElegantOTA) |
| 36 | 2026-05-09 | 🔴 | /log-Endpoint HTTP 500 | ✅ v4.3.6 |
| 43 | 2026-05-12 | 🔴 | Mechanik-Blockade im Player | ✅ v4.4.0-rc2 |
| 44 | 2026-05-12 | 🔴 | Lärm-Regression | ✅ v4.4.0-rc3 (500 RPM Hybrid) |
| 45 | 2026-05-12 | 🔴 | Performance-Mismatch (teigig) | ✅ v4.4.0-rc3 (maxAccel Cap) |
| 46 | 2026-05-14 | 🔴 | **Recorder Spinlock & Heap Fragmentation:** `Recorder::getCsv()` hielt motorMux über 600 Allocs (38 KB). | ✅ v4.4.8 (Streaming via `AsyncResponseStream` + Mini-Locks) |
| 47 | 2026-05-14 | 🔴 | **WD False-Positive (Low Freq):** 3-s-Fenster zu kurz für f=0.5 Hz. | ✅ v4.4.8 (Adaptives Fenster $W = \max(3s, 2/f)$ + WD_MIN_F_HZ=1.0) |
| 48 | 2026-05-14 | 🟡 | **Motor Naming Bug:** ASCII-Arithmetik `'X'+3` ergibt `[` statt `E`. | ✅ v4.4.8 (Explicit `v4::motorName(i)` Helper in Types.h) |
| 49 | 2026-05-14 | 🟡 | **NVS Latency (/bounds):** `/bounds`-Endpoint lud 5x NVS-Blobs direkt. | ✅ v4.4.8 (RAM-Cache in `Storage_Calib.cpp`) |
| 50 | 2026-05-14 | 🟡 | **Layout Shift (HIT Ping):** Text "HIT" im Player-Status änderte Spaltenbreite. | ✅ v4.4.8 (Grüner Dot vor Motorname via `.dot.hit` CSS) |
| 51 | 2026-05-12 | 🔴 | **Calib Stall-Schutz:** Grob-Suche hatte keine Live-Stall-Detection. | ✅ v4.4.15 (Live-Tacho-Check in `searchEntry`, RPM=0 → Abort) |
| 52 | 2026-05-14 | 🟡 | Flash Usage Trend (Partition-Limit) | 🔵 noted |
| 53 | 2026-05-14 | 🔴 | **Calib P1 Endstop Miss:** Motor erreichte Endstop nicht (Microstep-Cleanup fehlt). | ✅ v4.4.8 (Explicit `restoreDefaults` Helper) |
| 54 | 2026-05-15 | 🔴 | Speed-Slider Range / Engine-Cap / Contrast-Altlast | ✅ v4.4.14 |
| 56 | 2026-05-15 | 🔴 | **Telemetry Spinlock-Bug:** wie Bug 46. | ✅ v4.4.11 (Streaming implemented) |
| 58 | 2026-05-15 | 🔴 | Calib-Button Feedback verschluckt | ✅ v4.4.15 (Event-Delegation + Integer-Cast Fix) |
| 59 | 2026-05-15 | 🟡 | Log-Scrolling kaputt | ✅ v4.4.15 (Threshold 5px + requestAnimationFrame) |
| 62 | 2026-05-16 | 🔴 | **Motor Profile "unlearned":** Watchdog-Modell wurde in Characterization nicht befüllt. | ✅ v4.4.15 (runProfileTest implemented als Show 4/8) |
| 63 | 2026-05-16 | 🔴 | **cmd() String Index Bug:** JS-Fehler in INDEX_HTML behandelte Motor-ID als String, was 'XYZE'[m] zu undefined machte. | ✅ v4.4.15 |
| 60 | 2026-05-15 | 🔴 | **FreqSweep Deadlock:** Bisektion triggert Re-Sync bei winzigen Amps. | ✅ v4.4.15 (Amplitude-Guard > 30 steps) |
| 61 | 2026-05-15 | 🟡 | **Log-Artefakte (Bug 55):** Pfadnamen im Log statt Zahlen. | ✅ v4.4.8 (verschwunden durch Recorder Heap-Fix) |
| 55 | 2026-05-16 | 🟣 | **Adaptiver Min-Strom Matrix MS=[16,32,64,128,256]:** Player-Strom dynamisch nach Frequenz/Range/µStep, gegen StealthChop-Pfeifen bei Überstrom. | ✅ v4.4.15 — Schema 4005 (`silentCurrentMA[5]` in CalibrationData), `runSilentProfileTest` (Show 8/9) misst Mindeststrom pro µStep mit 10 % Headroom, `applySilentHardwareSettings` in Synthesis wählt MS/MA aus rpmMax + Matrix-Lookup. `Tmc::applyDefaults(idx, runMA, ms)` neue Signatur + `setCurrent`/`getRunCurrent`. |
| 64 | 2026-05-16 | 🔴 | **UI signalisiert nicht wenn Synth-Interlock blockiert.** | ✅ v4.4.18 (Synchroner 409-Check im WebServer) |
| 59 | 2026-05-15 | 🟡 | Log-Scrolling: User-Report „greift in v4.4.15 weiter nicht". | 🔴 re-open, unverifiziert. |
| 65 | — | — | siehe Bug 64 — gleicher Code-Pfad und Fix | merged into 64 |
| 66 | 2026-05-16 | 🔴 | **NVS-Schema-Migration 4004→4005 verliert Daten.** | ✅ v4.4.18 (Migration in `load` implementiert) |
| 67 | 2026-05-16 | 🟡 | **Mode-Switch wendet Silent-Matrix nie an (toter Code).** | 🔴 offen |
| 68 | 2026-05-16 | 🟡 | **v_peak-Formel: Faktor 1.047 = π/3 statt π/6 = 0.524.** | ✅ v4.4.18 (Faktor korrigiert) |
| 69 | 2026-05-16 | 🟡 | **Strukturelle Race: setPower→applyDefaults(MS=64) dann applySilentHardwareSettings(MS=16).** | ✅ v4.4.18 (applyDefaults aus setPower entfernt) |
| 70 | 2026-05-16 | 🟡 | **`silentCurrentMA[]` nur in /watchdog exponiert, nicht in /bounds.** | 🔴 offen |
| 71 | 2026-05-16 | 🟡 | **Fallback-Strom 800 mA hardcoded.** | 🔴 offen |
| 72 | 2026-05-16 | 🟣 | **Silent-Matrix ignoriert f-Achse — Strom konstant pro MS, unabhängig von Drehzahl.** | 🟣 Backlog |
| 73 | 2026-05-16 | 🔵 | **Datasheet-Insight: `intpol(true)` macht externes MS smoothness-irrelevant.** | 🔵 noted |
