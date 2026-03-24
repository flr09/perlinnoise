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

---

## 3. Anforderungen vs. Realität
*   **Ziel:** 800 RPM (42.666 sps).
*   **Status:** Aktuell unerreicht. Das System "erstickt" an der eigenen Diagnose (Telemetrie).
*   **Tacho-Stabilität:** Der Sensor arbeitet physikalisch korrekt, aber die Erfassung war durch CPU-Blockaden unzuverlässig. Die Hardware-Integrität von GPIO 15 bleibt unter Beobachtung (PNP-Vorschaden).

---

## 4. Git & Workflow
- Version auf 3.3.3 hochgestuft.
- Dokumentation der Jitter-Analyse abgeschlossen.
- Nächster Schritt: Code-Säuberung (v3.3.3-Build).
