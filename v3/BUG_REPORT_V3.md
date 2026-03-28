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

## Verifizierte Parameter (v3.7.29)
- **FW-Build:** SUCCESS
- **Methode:** Modularisierte Telemetrie-Handler, CORS-Fix für Live-Sync und Content-Disposition für CSV-Downloads.
- **Status:** Stabilisiert. Telemetry Viewer (v3.7.29) voll funktionsfähig (Live & Paste).

### [T28] Telemetrie-Ausfall & Live-Sync Bug
- **Symptom:** Weder Live-Daten noch CSV-Abruf im Telemetry Viewer möglich (v3.7.28).
- **Ursache:** 
    1. Fehlende `Access-Control-Allow-Origin: *` Header führten zu CORS-Fehlern beim lokalen Ausführen des Viewers.
    2. Fehlende `Content-Disposition` Header verhinderten korrekte Datei-Downloads.
    3. Syntax-Fehler und fehlendes Error-Handling im JavaScript des Viewers (v3.7.28).
- **Fix (v3.7.29):** 
    1. Vollständige Modularisierung der Telemetrie: Web-Handler nach `Telemetry.cpp` verschoben.
    2. CORS-Header zu `/status`, `/telemetry` und `/cmd` hinzugefügt.
    3. `telemetry_viewer.html` repariert: Paste-Area wiederhergestellt, robustere URL-Extraktion für `/status` und detailliertes Error-Reporting.
