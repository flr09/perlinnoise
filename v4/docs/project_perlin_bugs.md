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
