# Hardware Status Log - PerlinNoise Projekt (FYSETC E4)

## Vorfall: PNP statt NPN Endschalter
**Datum:** 2026-03-24
**Fehler:** Ein PNP-Endschalter wurde fälschlicherweise am `TACHO_PIN` (GPIO 15) betrieben.
**Komplikation:** PNP-Schalter schalten typischerweise die Versorgungsspannung (oft 12V oder 24V) auf den Ausgangssignalpin. Der ESP32 ist an seinen GPIOs jedoch nur **3,3V-tolerant**.
**Status:** Der Schalter wurde inzwischen durch einen korrekten NPN-Schalter ersetzt.

### Risikobewertung:
1.  **GPIO 15 Defekt:** Falls die Spannung am Pin zu hoch war, kann der interne Eingangstransistor oder der Pull-up-Widerstand des GPIO 15 zerstört worden sein.
2.  **Board-Instabilität:** In seltenen Fällen können Überspannungen an einem Pin auch andere interne Komponenten des ESP32 beeinträchtigen.

### Validierung (Dringend ausführen):
- In der V3-Testsuite (Motor-Lab) muss bei Betätigung des NPN-Schalters (Magnet an den Sensor halten) die Anzeige **"HIT"** im Web-Interface zuverlässig zwischen TRUE (rot) und FALSE wechseln.
- Bleibt die Anzeige immer auf "TRUE" (Low-Pegel am Pin), ist der Pin möglicherweise intern gegen GND "geschmolzen" oder der NPN-Schalter ist dauerhaft aktiv.
- Bleibt sie immer auf "FALSE", ist der Pin oder die Leiterbahn unterbrochen.

---
## Firmware-Stand (V3)
Die V3-Firmware dient primär der Validierung dieser Hardware-Komponente und der Ermittlung der maximalen Geschwindigkeiten der Motoren unter Last.
