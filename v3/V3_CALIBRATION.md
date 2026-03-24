# V3 Motor Calibration & Stress-Test Log

## Setup
*   **Board:** FYSETC E4
*   **Firmware:** V3.0.0-TESTSUITE
*   **Sensors:** NPN NO (Shared Bus on GPIO 15)

## 1. Sensor-Check
*   [ ] Motor X LED reagiert auf Magnet
*   [ ] Motor Y LED reagiert auf Magnet
*   [ ] Motor Z LED reagiert auf Magnet
*   [ ] Motor E LED reagiert auf Magnet
*   *Hinweis: Da alle am selben Bus hängen, sollten bei Trigger immer alle LEDs gleichzeitig leuchten.*

## 2. Homing-Kalibrierung (Park-Position)
*   **Ziel:** Motor findet Trigger und fährt auf 180° (800 Steps).
*   [ ] Motor X: Erfolg/Fehlgeschlagen
*   [ ] Motor Y: Erfolg/Fehlgeschlagen
*   [ ] Motor Z: Erfolg/Fehlgeschlagen
*   [ ] Motor E: Erfolg/Fehlgeschlagen

## 3. Stress-Parkour (Last-Test)
*Hier tragen wir ein, bei welcher Beschleunigung (Accel) und Geschwindigkeit (Speed) die Motoren mit den beladenen Tellern anfangen zu "springen" oder zu überschlagen.*

| Objekt-Gewicht | Max Speed (sps) | Max Accel (sps²) | Ergebnis |
| :--- | :--- | :--- | :--- |
| Leer | | | |
| Klein | | | |
| Mittel | | | |
| Groß | | | |
