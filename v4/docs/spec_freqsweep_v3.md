# 📊 Spezifikation: FreqSweep v3 (High-Freq & SG4-Fusion)

Dieses Dokument dient als Arbeitsanweisung für die Entwicklung von **v4.3.2+**. Ziel ist die Erweiterung des Frequenzgang-Tests bis 500 Hz für optische Vibrations-Effekte.

## 1. Problemstellung (Befund v4.2.2)
- **Tacho-Blindheit:** Ab ~10 Hz meldet der Tacho `0 Pulse`, da die mechanische Hysterese des Sensors größer ist als die Schwing-Amplitude ($amp \propto 1/f^2$).
- **Abbruch-Bug:** Die aktuelle Software interpretiert `0 Pulse` als Total-Stall und bricht den Test ab (ID 22). Vibrationen über 10 Hz werden so nie erfasst.

## 2. Phase A: Tacho-Cutoff Diagnostik (v4.3.2)
Bevor wir blind umschalten, messen wir die reale mechanische Grenze des Tachos.

- **Neuer Modus:** `runTachoCutoffDiagnostic()`
- **Parameter:** Frequenz-Sweep in 1-Hz-Schritten von 5 Hz bis 50 Hz. Amplitude pro Frequenz $\text{amp}(f) = \min(45°, \text{ampPhysMax}(f))$ mit $\text{ampPhysMax} = \text{FREQ\_ACCEL\_MAX}/(16 f^2)$ — das ist die größte Schwingweite, die der Motor bei $f$ noch sauber schafft. Konstantes 45° ist physikalisch unmöglich für $f > 14$ Hz (Z-Motor max ~80k sps, $4 \cdot 1037 \cdot 14 = 58$k sps + Beschleunigung).
- **Ziel:** Den Punkt finden, an dem die Pulse trotz physikalisch-maximaler Amplitude von $\geq$ swings/2 auf 0 fallen ($f_{cutoff}$).
- **Log:** `f | amp | pulses | swings | drift | sg`.
- **Drift-Telemetrie (Befund 2026-05-09):** Bei Schwingungen um ~10 Hz (Z-Mechanik, vgl. Bug 32) driftet der Motor-Mittelpunkt asymmetrisch aus dem Sensor-Sichtfeld, ohne dass es ein echter Stall ist. `drift = pos_end − edge_pos` wird mitgeschrieben, um „Hysterese-blind aber lebendig" (kleiner Drift) von „echter Total-Aussteiger" (großer Drift) zu unterscheiden.
- **Erwarteter Z-Motor-Befund** (extrapoliert aus FS2-Hardware-Test 2026-05-09): $f_c \approx 10$ Hz (bei f=10 noch p=3/5, bei f=12 schon p=0/6 in FS2 mit ampMax=312/217 Steps).

## 3. Phase B: StallGuard4-Integration (v4.3.2)
Da der Tacho oberhalb $f_{cutoff}$ blind ist, nutzen wir die **wicklungsgemessene** Last-Erkennung (Back-EMF) als Fallback-Sensor — **nicht als Ersatz**.

- **L3-API:** `Tmc::getSGResult(motorIdx)` implementieren (Register `SG_RESULT`, UART-Read).
- **Fusion-Decision-Tree (Strategie statt Spec-wörtlich, ersetzt naives „SG > 0 = lebt"):**
    1. **Tacho liefert Pulse** → Motor lebt, SG-Wert ignorieren (umgeht Bug 33: SG_RESULT bei oszillierender Bewegung konstant 0 trotz Bewegung).
    2. **Tacho blind ($f > f_{cutoff}$)** → SG_RESULT via UART als Lebenszeichen herziehen.
    3. **Beide 0 plus großer `meanPosDrift`** → echter Stall, Test abbrechen.
- **Panik-Polling statt Dauer-Read:** SG_RESULT-UART-Read kostet ~1.5 ms, bei 100 Hz Polling verschmiert das Sample über die Halbperiode (50 ms bei 10 Hz). Daher SG nur lesen, wenn der Tacho in der aktuellen Halbperiode keinen Puls geliefert hat. Unterhalb $f_{cutoff}$ läuft kein einziger UART-Read.
- **Hardware-Vorbehalt:** `TCOOLTHRS` muss > 0 gesetzt werden, damit SG_RESULT überhaupt einen sinnvollen Wert liefert. `SGTHRS=0` bleibt hart, damit der DIAG-Pin niemals feuert (DIAG ist auf der FYSETC E4 mit den MIN-Endstop-Traces verbunden — und unsere Tacho-Pins sind genau diese MIN-Pins). Unbestätigt bleibt, ob `TCOOLTHRS > 0` die im Original-Code dokumentierte Leckströmung verschärft → Phase A muss zuerst mit `TCOOLTHRS=0` ein Tacho-Baseline messen, bevor in Phase B umgeschaltet wird.

## 4. Phase C: 500 Hz High-Freq Sweep (v4.3.3)
Erweiterung des Rasters für optische Effekte.

- **Bänder:** 6, 8, 10, 12, 14, 16, 20, 26, 36, 50, 100, 200, 300, 400, 500 Hz.
- **Amplituden-Modell:** Physikalische Extrapolation via $1/f^2$ basierend auf den letzten "guten" Tacho-Werten (6+8 Hz).
- **Kein Abbruch:** Der Test läuft bis 500 Hz durch, solange StallGuard eine Wicklungsbewegung sieht.

## 5. Leitplanken (Bauhaus-Regeln)
- **Robustheit:** Keine Regelkreise. StallGuard dient nur als "Auge in die Wicklung" für den Test-Log.
- **Sicherheit:** `HARD_ACCEL_CAP` (500k) bleibt aktiv, um mechanische Resonanz-Katastrophen zu vermeiden.
- **Evidenz:** Alle $f_{cutoff}$ Werte werden in `CalibrationData` (NVS) gespeichert.

---
*Erstellt am 2026-05-06 von Gemini CLI als verbindliche Spezifikation für Claude.*
