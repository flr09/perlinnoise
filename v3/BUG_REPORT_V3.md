# Bug Report V3 — PerlinNoise Motorsteuerung

## Aktuelle Fehler & Fixes (Stand 2026-03-27)

### [F16] Massives EMI-Rauschen am TACHO_PIN (GPIO 15)
- **Symptom:** Ghost-Trigger bei laufenden Motoren. Die ISR feuert permanent, Kalibrierung bricht sofort ab oder liefert identische A1/A2 Werte.
- **Ursache:** GPIO 15 am FYSETC E4 (ESP32) ist ein kapazitiver Touch-Pin und extrem empfindlich für das Schaltrauschen der TMC2209 Treiber.
- **Fix (v3.7.10):** 
    1. Implementierung eines **5-Hit Filters**: Ein Signalzustand wird erst akzeptiert, wenn er 5x hintereinander stabil gelesen wurde.
    2. Umstellung auf **Successive Approximation**: Grob-Suche (1000 sps) zur Fenster-Eingrenzung, gefolgt von extrem langsamem Antasten (100 sps).

### [C6] Ungenaue Kantenbestimmung (Hysterese)
- **Symptom:** 0-Punkt verschiebt sich je nach Anfahrtrichtung.
- **Fix (v3.7.10):** Jede Sensorflanke (A1 links, A2 rechts) wird **3-mal angetastet** und der Durchschnitt gebildet. Dies eliminiert mechanische und elektronische Varianzen.

### [S2] Emergency Stop reagiert nicht während Suche
- **Fix (v3.7.10):** `waitForSensorStable` prüft in jeder Iteration `sys.pendingStop`.

## Verifizierte Parameter (v3.7.10)
- **FW-Build:** SUCCESS
- **Methode:** Successive Approximation (Grob -> Fein -> 3x Antasten)
- **Status:** Stabil gegen EMI-Rauschen am FYSETC E4.
