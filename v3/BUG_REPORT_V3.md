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

### [R5] Homing/Calib Instabilität in v3.7.15
- **Symptom:** Homing fährt unvorhersehbar oder bricht ab.
- **Ursache:** "Fast Homing" via ISR-Snapshot (v3.7.15) war zu anfällig für EMI-Peaks bei 800 sps.
- **Fix (v3.7.20):** 
    1. Revert auf **3.7.11 Robust Logic**.
    2. `touchEdge` nutzt wieder **3 Samples** und Stillstands-Position.
    3. `homeMotor` nutzt wieder die dedizierte `touchEdge`-Phase nach der Schnellsuche.
    4. EMI-Filter (5-hit, 100µs) aus 3.7.15 wurden für zusätzliche Stabilität beibehalten.

## Verifizierte Parameter (v3.7.31)
- **FW-Build:** SUCCESS
- **Methode:** Support for 'show' (Vorführmodus) in Telemetry Viewer autoSyncLoop.
- **Status:** Stabilisiert. Sowohl 'test' als auch 'show' (Vorführmodus) triggern nun den Live-Sync und finalen Abruf.

### [T31] Vorführmodus ('show') unsichtbar für Live Sync
- **Symptom:** Im Vorführmodus wurden keine Telemetriedaten automatisch geladen.
- **Ursache:** Die `autoSyncLoop` prüfte exklusiv auf den Status `test`. Der Status `show` (Vorführmodus) wurde ignoriert.
- **Fix (v3.7.31):** Erweiterung der Status-Prüfung um `show`. Einführung von `isActive` und `wasActive` Helfern zur sauberen Transitionserkennung.
