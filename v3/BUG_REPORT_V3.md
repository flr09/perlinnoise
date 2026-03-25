# Technischer Statusbericht & Bug-Report

**Firmware:** v3.5.4 | **Stand:** 2026-03-24

---

## 1. Kritische Analyse der Telemetrie (parcour_183755.csv)
*   **MASSIVER SCHRITTVERLUST:** Bei einem Soll von 300 RPM (16.000 sps) erreicht der Motor real nur ca. 700 sps.
*   **DROP-OUTS:** Lücken von bis zu 5,2 Sekunden in den Daten bestätigen, dass der Prozessor durch UART-Timeouts blockiert wird.
*   **JITTER-URSACHE:** Die Funktion `recordTelemetry()` fragt im Motor-Loop zu viele TMC-Register ab. Jede Abfrage blockiert Core 1 und verhindert das rechtzeitige Auslösen der Schritte.

---

## 2. Offene Bugs & Maßnahmen (v3.3.3)

| ID | Beschreibung | Maßnahme |
| :--- | :--- | :--- |
| **BUG-11** | **UART-Blocking** | **RADIKAL-FIX:** Entfernung aller TMC-Registerabfragen aus der `while`-Schleife. Telemetrie wird auf Core 0 ausgelagert oder auf ein Minimum reduziert. |
| **BUG-12** | **Library Limit** | `AccelStepper` stößt bei >15.000 sps an Grenzen. Prüfung auf `FastAccelStepper` oder Reduzierung der Microsteps auf 8 für hohe RPM. |
| **BUG-13** | **Modus-Jitter** | Automatischer Wechsel (TPWMTHRS) sorgt für Unruhe. Fixer Wechsel auf SpreadCycle vor dem SPEED-Lauf implementieren. |
| **BUG-14** | **Tacho-Aliasing** | **LÖSUNG:** Umstieg von Polling auf Hardware-Interrupts (ISR). Der Pin 15 triggert sofort eine Speicherung der Position, unabhängig von der Loop-Frequenz. |
| **BUG-15** | **Task-Blocking** | **FAIL (v3.3.5):** Ein `vTaskDelay(1)` in der Motor-Task begrenzte das Tempo künstlich auf 1000 sps. **FIX:** Entfernung der Delays während der Fahrt (v3.3.7). |
| **BUG-16** | **Hardware-Enable** | **FAIL (v3.3.7):** Falsche Logik am GPIO 25 (HIGH statt LOW) führte dazu, dass der Motor stromlos drehte. **FIX:** Invertierung der Enable-Logik in v3.3.8. |

---

## 3. Anforderungen vs. Realität
*   **Ziel:** 800 RPM (42.666 sps).
*   **Status:** v3.3.8 erreicht nun die notwendige CPU-Frequenz und Hardware-Bestromung.
*   **Tacho-Stabilität:** Dank ISR (v3.3.5) ist die Drift-Messung bei hohen RPM nun physisch möglich.

---

## 4. Bugs v3.5.x (FastAccelStepper-Migration)

| ID | FW | Beschreibung | Schwere | Fix |
|----|----|--------------|---------|-----|
| **A1** | 3.5.2 | ISR-Crash: `tachoISR` rief `stepper->getCurrentPosition()` auf — nicht ISR-sicher | 🔴 Crash | Entfernt, nur `pulseCount++` |
| **A2** | 3.5.2 | Mutex 10ms: `updateTelemCache` wartete nur 10ms, `applyDriverSettings` hält bis 16ms | 🟠 Deadlock | 100ms |
| **A3** | 3.5.2 | UART von Core 1: `learnSGProfile` las `driverX.SG_RESULT()` direkt | 🟠 Jitter | Auf `tCache.sg` umgestellt |
| **B1** | 3.5.3 | `xSemaphoreCreateMutex()` global: FreeRTOS-Heap beim globalen Konstruktor noch nicht bereit → Panic | 🔴 Boot-Crash | In `initMotors()` verschoben |
| **B2** | 3.5.3 | `learnSGProfile` kein stopMove: Motor lief weiter → `setMicrosteps(64)` übersprungen | 🟠 Silent fail | `stopMove()` vor setMicrosteps |
| **B3** | 3.5.3 | `runCoastTest` kein stopMove: Motor lief nach Coast-Test endlos weiter | 🟠 Silent fail | `stopMove()` am Ende |
| **D1** | 3.5.4 | `characterizeSensor`: Nach `setSpeedInHz(200)` kein `runForward()`/`runBackward()` → Motor blieb bei 400Hz → falscher `triggerCenter` | 🔴 Falsche Funktion | `runForward()`/`runBackward()` nach Speed-Wechsel eingefügt |
| **D2** | 3.5.4 | `learnSGProfile`: `runForward()` in jeder Iteration der Messschleife → FastAccelStepper interpretiert als neuen Befehl → Jitter alle paar ms | 🟠 Jitter | `runForward()` aus Schleife heraus, einmaliger Aufruf |

---

## 5. Git & Workflow

| Version | Commit | Beschreibung |
|---------|--------|--------------|
| 3.3.3 | — | Jitter-Audit dokumentiert |
| 3.4.0 | `54180c3` | Comprehensive logic overhaul, measureActualRpm, SG-Learn fixes |
| 3.5.1 | `8307f17` | FastAccelStepper API final corrections |
| 3.5.2 | `61b0e50` | ISR, Mutex, UART/Core1 fixes + Winkelmarker |
| 3.5.3 | `566b3e6` | Boot-Crash (Mutex global) + 4 Logic-Bugs |
| 3.5.4 | — | D1/D2: characterizeSensor + learnSGProfile runForward-Bugs |
