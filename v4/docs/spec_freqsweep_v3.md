# 📊 Spezifikation: FreqSweep v3 (High-Freq & SG4-Fusion)

Dieses Dokument dient als Arbeitsanweisung für die Entwicklung von **v4.3.2+**. Ziel ist die Erweiterung des Frequenzgang-Tests bis 500 Hz für optische Vibrations-Effekte.

## 1. Problemstellung (Befund v4.2.2)
- **Tacho-Blindheit:** Ab ~10 Hz meldet der Tacho `0 Pulse`, da die mechanische Hysterese des Sensors größer ist als die Schwing-Amplitude ($amp \propto 1/f^2$).
- **Abbruch-Bug:** Die aktuelle Software interpretiert `0 Pulse` als Total-Stall und bricht den Test ab (ID 22). Vibrationen über 10 Hz werden so nie erfasst.

## 2. Phase A: Tacho-Cutoff Diagnostik (v4.3.2)
Bevor wir blind umschalten, messen wir die reale mechanische Grenze des Tachos.

- **Neuer Modus:** `runTachoCutoffDiagnostic()`
- **Parameter:** Konstante, große Amplitude (z. B. 45°), Frequenz-Sweep in 1-Hz-Schritten von 5 Hz bis 50 Hz.
- **Ziel:** Den Punkt finden, an dem die Pulse von 4/4 auf 0/4 fallen ($f_{cutoff}$).
- **Log:** `f | pulses | swings | SG_RESULT`.

## 3. Phase B: StallGuard4-Integration (v4.3.2)
Da der Tacho blind ist, nutzen wir die **wicklungsgemessene** Last-Erkennung (Back-EMF).

- **L3-API:** `Tmc::getSGResult(motorIdx)` implementieren (Register `SG_RESULT`).
- **Trigger:** Ab $f > f_{cutoff}$ übernimmt StallGuard die Stall-Detection.
- **Kriterium:** Solange `SG_RESULT > 0`, vibriert der Motor (er lebt). Bei `SG_RESULT == 0` ist er physisch blockiert (Stall).

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
