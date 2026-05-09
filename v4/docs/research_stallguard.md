# 🔬 Research Deep-Dive: StallGuard4 on TMC2209 (FYSETC E4)

## 1. System-Parameter (Ist-Stand)
- **Controller-Board:** FYSETC E4 (ESP32-WROOM-32)
- **Treiber-Chip:** TMC2209 (UART-Mode aktiv via L3_driver)
- **Netzteilspannung:** 24V DC (Standard für E4-Performance)
- **Motoren:** NEMA14 Pancake (0.92A RMS)
- **Zusatz-Sensorik:** LJ12A3-4-Z/BX (Induktiver Tacho) an GPIO 15/34/35

---

## 2. StallGuard: v2 vs. v4
Die Trinamic-Dokumentation (AN-002) unterscheidet primär nach dem Modus:

### StallGuard2 (SG2)
- Optimiert für **SpreadCycle** (den kräftigeren, aber lauteren Chopper-Modus).
- Misst die Last über den gesamten Phasenstrom-Verlauf.
- Verfügbar in TMC2130, TMC2208/9 (als Fallback).

### StallGuard4 (SG4) — Unser Ziel
- Optimiert für **StealthChop2** (den Silent-Mode, den wir für organische Perlin-Bewegungen nutzen).
- **Funktionsweise:** SG4 misst die Phasenverschiebung zwischen Strom und Spannung (Back-EMF) am Nulldurchgang. 
- **Vorteil:** Es funktioniert perfekt im geräuschlosen Modus. Da wir in v4 auf "Glätte" setzen, ist SG4 die einzig logische Wahl.

---

### 3. Integration in die Bewegungslogik (L5b)

Unsere aktuelle Logik (`Synthesis.cpp`) berechnet Zielpositionen im 100Hz-Takt. StallGuard4 könnte dies zu einem **Closed-Loop-System ohne Encoder** erweitern:

#### A. Die StealthChop-Exklusivität (KRITISCH)
- **Befund:** StallGuard4 funktioniert **nur** im StealthChop-Modus. Sobald die Geschwindigkeit `TPWMTHRS` überschreitet und der Treiber auf SpreadCycle umschaltet, wird SG4 deaktiviert.
- **Problem:** In v4.3.1 läuft der Square-Mode fest in SpreadCycle — wir haben dort also **kein Stall-Feedback**.
- **Lösung für v4.3.2:** Um SG4 zu nutzen, müssen wir in StealthChop bleiben. Wir sollten `TPWMTHRS` so hoch wie möglich setzen oder ganz auf 0xFFFFF (immer StealthChop), solange das Drehmoment reicht.

#### B. Das "Jerk"-Phänomen (Umschalt-Ruck)
- **Befund:** Das Umschalten zwischen StealthChop und SpreadCycle bei hoher Geschwindigkeit verursacht einen mechanischen Ruck (Jerk), da sich die Regelung von Spannung auf Strom ändert.
- **Empfehlung:** Umschaltpunkte (`TPWMTHRS`) sollten idealerweise unter **30-50 RPM** liegen, um die Mechanik zu schonen. Für Perlin-Installationen ist es oft besser, dauerhaft in einem Modus zu bleiben.


### B. Die "Tacho-Fusion" (Evidenzbasiert)
Einzigartig an unserem Setup ist der induktive Tacho.
- **Kalibrierung:** Wir können den `SGTHRS` (Stall-Threshold) automatisch lernen. Der Motor fährt in den Stall, der Tacho meldet `pulses=0`, und im selben Moment lesen wir den StallGuard-Wert aus. 
- **Ergebnis:** Ein absolut präzises Stall-Modell pro Motor, das Alterung und Temperatur berücksichtigt.

---

## 4. Konfiguration & Register (L3_driver)

Um SG4 auf dem E4 scharf zu schalten, müssen folgende Parameter in `Tmc2209.cpp` angepasst werden:

| Register | Wert (geschätzt) | Bedeutung |
|---|---|---|
| `TCOOLTHRS` | ~20-50 | Unterhalb dieser Geschwindigkeit (in TSteps) ist SG inaktiv. |
| `SGTHRS` | 0..255 | Die Empfindlichkeit. Muss via Tacho-Lernlauf (L5a) ermittelt werden. |
| `SG_RESULT` | Read-only | Der aktuelle Lastwert (0 = Blockade). |

---

## 5. Vision: "Der Folgeroboter" (v5.x)

Für eine zukünftige Hardware-Generation (z.B. mit TMC5160 oder AS5600) bietet StallGuard4 folgende Möglichkeiten:

1. **Sensorless Homing:** Die induktiven Sensoren könnten entfallen, der Motor findet seine Endanschläge durch "Anstoßen" (Sanftes Auffahren mit SG4-Erkennung).
2. **Kollisionsschutz:** Ein "E-Stop" wird ausgelöst, sobald jemand in die Mechanik greift — ohne zusätzliche Lichtschranken.
3. **Resonanz-Monitoring:** SG4-Werte schwanken bei mechanischen Resonanzen. Wir könnten "tote" Frequenzbänder im Player automatisch überspringen.

---

## 6. Nächste Schritte für v4.2.2 (StallGuard-Prep)
1. **L3-Erweiterung:** `Tmc2209.h` bekommt `getSGResult(motorIdx)`.
2. **L5a-Test:** Neues Testprogramm `Prog_SgCalibration`, das den SG-Wert gegen den Tacho-Status bei verschiedenen RPMs mappt.
3. **Telemetry:** Einbau des SG-Werts in den CSV-Stream (`Telemetry.cpp`), um Last-Kurven in Excel/Canvas zu analysieren.

---
*Dokument erstellt am 2026-05-06 von Gemini CLI als strategische Roadmap für Stall-Monitoring.*
