# FSD — PerlinNoise v4 (Modular)

**Stand:** 2026-05-16 — Firmware **v4.4.28** auf Hardware. Phase 10.1 (Stabilität) + 10.2 (Silent Mode) abgeschlossen, Phase 10.3 (GUI-Härtung) begonnen.
**Branch:** `v4-modular`
**Status:** Systemstabilität unter UI-Last (Slider, Polling, Preview) durch Streaming-Endpoints + Slider-Debounce + DOM-Stabilität. Watchdog MS-invariant.

---

## 9. Phasen-Plan (Fortsetzung)

### Phase 10.1 — Stabilitäts- & Safety-Sicherung (v4.4.8)

**Ziel:** Behebung der v4.4.7-Stabilitätsprobleme und Vorbereitung auf Phase 11.

**Priorisierte Fixes:**

1.  **Bug 46 (Recorder Stability):**
    - **Problem:** `Recorder::getCsv()` blockiert durch Spinlock und riesige String-Allokation den Kernel und fragmentiert den Heap.
    - **Lösung:** Umstellung auf `AsyncResponseStream` (Streaming ohne Riesen-String) und Minimierung der Lock-Zeit (Snapshot-Copy der Samples).
2.  **Bug 47 (Watchdog Robustness):**
    - **Problem:** Fehlalarme bei $f < 1Hz$ durch zu kurzes 3s-Fenster.
    - **Lösung:** Adaptives Fenster oder "Wait-for-First-Schwingung"-Logik.
3.  **Bug 51/53 (Calibration Integrity):**
    - **Problem:** Stall-Gefahr in Calib ohne Schutz; Microstep-Leak vom Player stört Calib-Präzision.
    - **Lösung:** Explizites µStep-Reset vor Calib; Stall-Monitor (SG oder Tacho) während der Grob-Suche.
4.  **Polish (Bugs 48, 49, 50):**
    - Motor-Naming fixen (`X, Y, Z, E`).
    - `/bounds` NVS-Caching.
    - Layout-Shift "HIT" durch farbigen Dot ersetzen.

**Deliverables:**
- Firmware v4.4.8 (stable)
- Dokumentation der Messwerte (Flash-Usage, Heap-Stand)

**Tests:**
- T10.1.1 Recorder-Download während aktivem Noise-Mode (kein WiFi-Drop).
- T10.1.2 0.5 Hz Square-Wave über 60s ohne WD-Trigger.
- T10.1.3 Calib nach Player-Stopp (verifiziert µStep-Umschaltung).

### Phase 10.2 — Silent Mode Optimization (v4.4.16)

**Ziel:** Maximale Laufruhe und Präzision bei allen Bewegungsgeschwindigkeiten durch dynamische Hardware-Settings.

**Implementierung:**
1.  **Silent-Matrix:** Neue Datenstruktur `silentCurrentMA[5]` für Microstep-Level [16, 32, 64, 128, 256].
2.  **Silent-Parcours:** `runSilentProfileTest` ermittelt den Mindeststrom pro MS-Level bei langsamer Fahrt über den Sensor.
3.  **Dynamisches Scaling:** Die Engine (`Synthesis`) berechnet bei jedem Start/Wechsel die Peak-RPM und wählt das höchste gelernt MS-Level, das das ESP32-SPS-Limit (200kHz) nicht überschreitet.

**Deliverables:**
- Firmware v4.4.16
- Automatisches High-Res-Switching (bis 256 MS) bei langsamen Wellen (z.B. 0.01 Hz).

### Phase 10.3 — GUI-Härtung & Crash-Behebung (v4.4.23 → v4.4.28)

**Ziel:** Player-Crashes unter UI-Last beheben, Slider-Verhalten ehrlich machen, Watchdog mit Silent-Matrix kompatibel.

**Bugs behoben:**
- **Bug 79** (v4.4.23): Watchdog-Schwellwert MS-invariant (Grad statt Steps). Mit Silent-Matrix bei MS=128 hatte der 250-Step-Threshold bei kleinster Range gegriffen → false-STOP. Jetzt 28°-deg-Schwellwert.
- **Bug 76** (v4.4.24): `/preview` von String-Build (~80 KB/s Heap-Churn bei 10 Hz Noise) auf `AsyncResponseStream`. Live-Stress mit 30 parallelen Requests ohne Reboot.
- **Bug 77** (v4.4.25): Logger eigener `loggerMutex` statt `uartMutex`-Sharing. Bug-74-Fix hatte TMC-Operationen unter Log-Last blockiert.
- **Bug 78** (v4.4.26): `renderMotors` Update via `textContent`/`className` statt `innerHTML`-Rebuild. Buttons bleiben stabil, keine Click-Race.
- **Bug 69** (v4.4.27, 3. Iteration): `setPower` ruft `applyDefaults` nur noch bei Erst-Power-On (`currentMA==0`). Race bei Re-Power weg, Freeze-Schutz bleibt.
- **Bug 67, 70, 71, 80** (v4.4.28): Polish-Bündel — toten Mode-Switch-Call entfernt, `DEFAULT_SAFE_CURRENT_MA` zentralisiert, `silentMA` in `/bounds`, Slider-Flood gestoppt (`oninput`→lokal, `onchange`→Server).

**Deliverables:**
- Firmware v4.4.28 (Hardware-läuft, ungetaggt — wartet auf User-Verifikation)

### Phase 11 Plan — Sequencer (v4.5.0) — geplant
