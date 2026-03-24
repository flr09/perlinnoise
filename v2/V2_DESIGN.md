# V2 Design & Fahrplan: Optical Bench

## 1. Favoriten-System (GEPLANT)
*   **Konzept:** "Glücksmomente" (bestimmte Linsen-Stellungen) werden bevorzugt angefahren.
*   **Bias:** Einstellbare Wahrscheinlichkeit (z.B. 70% Favs, 30% Perlin-Drift).
*   **Speicherung:** JSON-Liste im NVS/LittleFS.

## 2. Motor-Testsuite (AKTUELL)
*   **Ziel:** Ermittlung von `maxSpeed` und `maxAccel` für schwere Objekte.
*   **Tacho-Validierung:** Nutzung des GX-H8A (GPIO 15) zur Erkennung von Schrittverlusten und Trägheitsnachlauf.
*   **TMC-Tuning:** Dynamische Umschaltung StealthChop <-> SpreadCycle.
*   **Ergebnis:** Automatisch ermittelte Grenzwerte werden für den stabilen Betrieb gespeichert.

## 3. Infrastruktur (ERLEDIGT)
*   **OTA:** Kabelloses Flashen via WLAN (perlin / 12345678).
*   **Dual-Core:** Core 0 (System/OTA), Core 1 (Motor-Timing/Noise).
*   **V1-Schutz:** Original-Code ist gesperrt und unberührt.
