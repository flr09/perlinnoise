# FSD — PerlinNoise v4 (Modular)

**Stand:** 2026-05-14 — Firmware **v4.4.7** auf Hardware. Phase 10 (2D-Spatial) weitgehend abgeschlossen.
**Branch:** `v4-modular`
**Status:** Systemstabilität unter Last (Recorder) und WD-Robustheit bei niedrigen Frequenzen sind die nächsten Ziele.

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

---
*(Rest der FSD bleibt unverändert)*
