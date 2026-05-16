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
| 67 | 2026-05-16 | 🟡 | **Mode-Switch wendet Silent-Matrix nie an (toter Code).** | ✅ v4.4.28 — Option (c) gewählt: toten `applySilentHardwareSettings`-Call aus Mode-Switch-Branch entfernt, durch erklärendes Kommentar ersetzt. µStep-Wechsel im laufenden Synth-Mode ist TMC-seitig nicht atomic (Glitch-Risiko). Silent-Matrix gilt jetzt explizit nur ab Player-Start. `applyEngineCap`/`applyChopperMode` bleiben reaktiv (UART-atomic). Keine Verhaltensänderung. |
| 68 | 2026-05-16 | 🔵 | **v_peak-Formel-Diagnose revidiert.** Engine-Konvention `rangeDeg = Peak` (Synthesis.cpp:560 `target = val × effRangeDeg × stepsPerDeg`, val ∈ [-1,+1] → Motor von -rangeDeg° bis +rangeDeg°). Damit ist `rpmMax = rangeDeg × f × 1.047 (π/3)` **korrekt** für Peak-Konvention. Mein „Faktor 2× zu hoch"-Befund war falsch. | 🔵 noted — kein Code-Fix nötig. **Aber**: UI in `applyLabels` Z. 370 zeigt `'±'+(rng/2)+'° ('+rng+'° pp)'` — das interpretiert rng als peak-to-peak. Convention-Mismatch (Engine=peak, UI=pp). Bei Slider-Wert 300 schwingt der Motor wirklich ±300° (600° pp), UI zeigt aber „±150° (300° pp)". Separate UI-Frage, kein Eintrag eröffnet bis Chris mündlich entscheidet welche Konvention er meint. |
| 69 | 2026-05-16 | 🟡 | **Strukturelle Race: setPower→applyDefaults(MS=64) dann applySilentHardwareSettings(MS=16).** Zwei UART-Writes hintereinander. | ✅ v4.4.27 — dritte Iteration: v4.4.18 entfernte applyDefaults aus setPower → Freeze (currentMA=0), wieder reingebracht mit „Bug-69-rev"-Kommentar. v4.4.27 jetzt **bedingt**: `if (currentMA[motorIdx] == 0) applyDefaults(motorIdx);` — Erst-Power initialisiert (Freeze-Schutz), Re-Power skippt (Aufrufer setzt MS/MA selbst, eine UART-Write). Live-verifiziert M0. |
| 70 | 2026-05-16 | 🟡 | **`silentCurrentMA[]` nur in /watchdog exponiert, nicht in /bounds.** | ✅ v4.4.28 (vorher schon in WebServer.cpp eingebaut). Live-verifiziert: `/bounds` enthält `"silentMA":[110,110,110,110,0]` für MZ (Silent-Profile gelernt für MS=16/32/64/128, MS=256 noch offen). |
| 71 | 2026-05-16 | 🟡 | **Fallback-Strom 800 mA hardcoded an 4 Stellen.** | ✅ v4.4.28 — `v4::DEFAULT_SAFE_CURRENT_MA` in Types.h zentral, Verweis auf Memory Bug 5 (Pancake-Hard-Cap 900 mA). 4 Call-Sites umgestellt: Synthesis.cpp Fallback, Tmc2209.h zwei Default-Args, Calibration.cpp explicit. EdgeTouch-`800` (SPS-Wert) NICHT angefasst — unrelated. |
| 72 | 2026-05-16 | 🟣 | **Silent-Matrix ignoriert f-Achse — Strom konstant pro MS, unabhängig von Drehzahl.** | 🟣 Backlog |
| 73 | 2026-05-16 | 🔵 | **Datasheet-Insight: `intpol(true)` macht externes MS smoothness-irrelevant.** | 🔵 noted |
| 74 | 2026-05-16 | 🔴 | **Logger Spinlock Heap Corruption:** `Logger::addLog` nutzt `substring` (Heap) in `portENTER_CRITICAL`. Ursache für Reboot bei `RT: saved`. | ✅ v4.4.22 (Migration zu `uartMutex` / `xSemaphoreTake`) |
| 75 | 2026-05-16 | 🔴 | **Silent Floor too low:** 100mA führen bei 2.0 Hz zum Stall am Z-Motor (ratio=0.32). | ✅ v4.4.22 (Min-Floor auf 200mA erhöht) |
| 76 | 2026-05-16 | 🔴 | **`/preview` Heap-Churn pro 100 ms (gleiche Familie wie Bug 46/56).** Vorher: `String json; json.reserve(n*4+64)` + 1024× String-Appends + Response-Copy = ~80 KB/s Heap-Churn bei 10 Hz Polling. | ✅ v4.4.24 — `AsyncResponseStream` mit byte-weise `print()`/`printf()`, kein voll-String. Live-verifiziert: 428 B Response @ 50 ms, 30 parallele Requests ohne Reboot (Slots voll → 22 timeouts, kein Crash). Race `getPreviewBytes` ↔ Synthesis-Tick auf Core 1 (rt+ne ohne Lock) bleibt strukturell — bekannt, separate Diskussion (nicht crash-kritisch, da rt+ne nur einfache Float-Reads). |
| 77 | 2026-05-16 | 🔴 | **Logger-Mutex-Contention mit TMC-UART.** Bug-74-Fix migrierte Logger fälschlich auf `Sync::uartMutex` — derselbe Mutex, der TMC-Kommandos schützt. `Logger::getBuffer` macht `out = buffer` (Heap-Copy bis 6 KB) unter dem Mutex; bei /status 10 Hz blockierte das TMC-Operationen. | ✅ v4.4.25 — neuer `Sync::loggerMutex` (eigener FreeRTOS-Mutex), Logger.cpp komplett auf den umgestellt. Live-verifiziert: Boot-Log voll vorhanden, kein Timeout-Verhalten. TMC-UART und Logger laufen jetzt parallel ohne gegenseitige Blockierung. |
| 78 | 2026-05-16 | 🟡 | **`renderMotors` DOM-Thrashing bei jedem /status-Tick.** Vorher `g.innerHTML = h` ersetzte 4 Motor-Cards mit 8-16 Buttons pro 100 ms → Click-Events während Rebuild verloren (Bug-58/63-Familie). | ✅ v4.4.26 — DOM einmal in `buildMotors()` aufgebaut (Buttons mit stabilen IDs `b_<a>_<i>`, Werte-Elemente `pos_<i>`, `spd_<i>`, `aux_<i>`, `dot_<i>`, `st_<i>`). `renderMotors` macht jetzt nur `textContent`/`className`-Updates — keine DOM-Allokation, keine Click-Race. |
| 79 | 2026-05-16 | 🔴 | **Player-Watchdog feuert false-positive nach Range-Cap.** Live-Log 2026-05-16 v4.4.22: `Range-Cap MZ: 300° → 26° @ 2.0 Hz` gefolgt von `PLAYER WD MZ: ratio=0.00`. Korrektur zur ersten Diagnose: `capWaveRangeByFreqAmp` war bereits in `tickWatchdog` aktiv, aber **`WD_MIN_AMP_STEPS=250` war nicht MS-invariant**. Bei MS=128 (Silent-Matrix Bug 55) wird `stepsPerRev=25600` statt 3200 → `demandedHalfAmp = 26° × 25600/360 = 1849 Steps` ≫ 250 → WD aktiviert sich auch unter Sensor-Hysterese. | ✅ v4.4.23 — Threshold auf `WD_MIN_AMP_DEG = 28.0°` (MS-invariant). Vergleich jetzt `effRangeDeg < WD_MIN_AMP_DEG`. Bei MS-Wechsel ändert sich nichts mehr am Verhalten. |
| 80 | 2026-05-16 | 🟡 | **Slider-Flood saturierte AsyncWebServer.** Vorher `oninput → setP → /set` pro Drag-Pixel = 200+ HTTP-Requests pro Slider-Drag. | ✅ v4.4.28 — Slider-Inputs gesplittet: `oninput → setPLocal` (nur lokales CFG + Coupling + Labels, kein Server-Call), `onchange` (mouseup) → `setP` (Server-Push). Engine hat authoritative Caps (Bug 54), Status-Polling synct den finalen Wert. Reduziert /set-Traffic während Drag von ~200 auf 1. Teile (b) /preview-Pollrate + (c) drainBuffer **nicht** umgesetzt — separate Punkte falls noch Lag spürbar. |
