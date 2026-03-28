# SensorCalib Modul — calib final Dokumentation

Dieses Modul bietet eine hochrobuste, EMI-resistente Kalibrierung und Homing-Logik für ESP32-Motorsteuerungen mit dem FastAccelStepper-Framework.

## Kern-Features
- **3-Touch calib final Standard:** Höchste Präzision bei der Kalibrierung durch 3-faches Antasten und Mittelwertbildung.
- **Optimiertes Homing:** Nutzt gespeicherte Kalibrierungsdaten (`triggerStartDeg`), um nach einer Kantenbestätigung direkt auf 0° zu fahren.
- **Hardware-Abstraktion:** Getrennt von der Haupt-Motorsteuerung, kommuniziert über definierte Funktionen.

## Integration in neue Projekte

### 1. Voraussetzungen (Dependencies)
Das Modul erwartet folgende globale Strukturen und Zeiger:
- `FastAccelStepper* stepper`: Der aktive Motor.
- `uint16_t stepsPerRev`: Aktuelle Auflösung (z.B. 12800 bei 64MS).
- `SystemState sys`: Beinhaltet `sys.cal[0]` (CalibrationData) und `sys.pendingStop`.

### 2. Benötigte Hardware-Brücken
Das Projekt muss folgende Funktionen bereitstellen (Linker-Ebene):
- `void addLog(String msg)`: Für Status-Ausgaben.
- `void saveCalibration(int i)`: Zum permanenten Speichern im NVS.
- `void setMotorPower(int i, bool on)`: Hardware-Enable der Treiber.
- `void setMicrosteps(uint16_t ms)`: Umschalten der Auflösung.
- `void applyDriverSettings(uint16_t runMA)`: Strom & TMC-Parameter setzen.

### 3. API-Aufrufe
- `runSensorCalibration(int i)`: Startet den kompletten 4-Phasen-Lauf zur Mittenbestimmung.
- `runMotorHoming(int i)`: Synchronisiert das Koordinatensystem und fährt auf 0°.

## Phasen-Logik der Kalibrierung
1. **P1 (Grob):** Schnelle Suche CW (1000 sps) bis Eintritt.
2. **P2 (Austritt):** CW (500 sps) bis der Sensor wieder HIGH wird.
3. **P3 (Rechts):** CCW (100 sps) 3-faches Antasten der rechten Kante (A2).
4. **P4 (Links):** CW (100 sps) 3-faches Antasten der linken Kante (A1).
5. **Final:** Berechnung von `center = (a1+a2)/2` und Speicherung der Offsets in Grad.
