# Technischer Statusbericht & Bug-Report

**Firmware:** v3.6.7 | **Stand:** 2026-03-26
**Status:** ✅ FUNKTIONAL (Hardware-Zähler & Signal-Routing verifiziert)

---

## 1. Analyse & Korrekturen (v3.6.7)

| ID | FW | Schwere | Beschreibung | Fix / Befund |
|----|----|---------|--------------|--------------|
| **F2** | — | 🔴 Halluz. | **Pin-Swap Irrtum:** Die Annahme, Pins seien 26/27, war falsch. Die Hardware (v2-Baseline) nutzt **27/26**. | Korrektur in v3.6.7 (27/26 sind korrekt). |
| **F6** | 3.6.7 | ✅ Fix | **Matrix-Initialisierung:** `pcnt_unit_config()` überschreibt RMT-Output, wenn es danach aufgerufen wird. | **Lösung:** PCNT wird nun *vor* FastAccelStepper initialisiert. FAS übernimmt am Ende die Matrix-Hoheit für den Output-Pfad. |
| **F7** | 3.6.7 | 🕒 Offen | **UI-Lüge:** HTML in `main_v3.cpp` zeigt noch `v3.5.4`. | Synchronisierung mit `FW_VERSION` ausstehend. |

---

## 2. Der aktuelle Stand (v3.6.7)

### A. Hardware-Signal-Integrität
*   Der PCNT-Hardware-Zähler wird jetzt als passiver "Schatten-Zähler" vor dem Stepper-Generator initialisiert.
*   Dies verhindert, dass der PCNT den Pin als reinen Input blockiert.
*   Die Pins sind wieder auf dem v2-Standard: **X_STEP=27, X_DIR=26**.

### B. Kalibrierung & Präzision
*   `tachoISR` liest den PCNT-Zähler direkt über das Hardware-Register (`PCNT.cnt_unit[0].val`). Dies ist 100% ISR-safe und cache-unabhängig.
*   Der systematische `yield()`-Fehler (ca. 1-5 Schritte Versatz) ist damit eliminiert.

---

## 3. Historische Referenz

| Version | Status | Grund für Probleme |
|---------|--------|---------------------|
| 3.6.1 | 🔴 Defekt | PCNT-GPIO Konflikt |
| 3.6.2-5 | 🔴 Defekt | Pin-Swap Experimente (falsche Pins 26/27) |
| **3.6.7** | ✅ **OK** | Korrekte Reihenfolge (PCNT vor FAS) + Original-Pins (27/26) |
