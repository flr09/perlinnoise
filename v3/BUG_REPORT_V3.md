# Technischer Statusbericht & Bug-Report

**Firmware:** v3.3.3 (Jitter-Audit) | **Stand:** 2026-03-24

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

## 4. Git & Workflow
- Version auf 3.3.3 hochgestuft.
- Dokumentation der Jitter-Analyse abgeschlossen.
- Nächster Schritt: Code-Säuberung (v3.3.3-Build).
