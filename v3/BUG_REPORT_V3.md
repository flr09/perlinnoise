# Technischer Statusbericht & Bug-Report

**Firmware:** v3.1.6 (Modular / Single-Motor) | **Stand:** 2026-03-24

---

## 1. System-Architektur
- **`MotorControl.h/cpp`**: Hardware-Abstraktion für FYSETC E4. Aktuell auf **Motor X (Index 0)** beschränkt.
- **`NoiseEngine.h`**: Mathematik-Modul (Simplex).
- **`main_v3.cpp`**: Webserver, OTA-Management, Task-Orchestrierung.

---

## 2. Status der Behebung (v3.1.6)

| ID | Beschreibung | Status |
| :--- | :--- | :--- |
| BUG-01 | Homing Timeout (15s) | Behoben |
| BUG-02 | platformio.ini Pfadtrennung | Behoben |
| BUG-03 | ArduinoOTA Initialisierung | Behoben |
| BUG-04 | runInertiaTest Guards | Behoben |
| BUG-05 | sys.log Mutex-Schutz | Behoben |
| BUG-06 | WiFi AP-Fallback | Behoben |
| BUG-07 | characterizeSensor Mutex | Behoben |
| BUG-08 | Unused NoiseEngine entfernt | Behoben |
| BUG-09 | Nav-Links korrigiert | Behoben |
| BUG-10 | UTF-8 Encoding & Meta-Tags | Behoben |
| NEW-01 | Motor "Always On" Bug | Behoben (via `toff` & Enable-Pin) |
| NEW-02 | Auto-Reboot nach Update | Behoben (via `setAutoReboot`) |

---

## 3. Aktuelle Parcours-Konfiguration (v3.1.6)

### Ein-Motor-Betrieb (Motor X)
Um Interferenzen zu vermeiden, sind die Treiber Y, Z und E in der Firmware deaktiviert. Details in `SINGLE_MOTOR_MODE.md`.

### CALIB SENSOR (Präzise)
Fährt den Sensor von beiden Seiten (CW/CCW) an, berechnet das mathematische Zentrum und ermittelt den Backlash.

### START PARCOUR
Kombiniert zwei Phasen:
1. **RPM-Check**: +100 RPM Schritte bis 2000 RPM. Nach jedem Run (2 Runden) erfolgt ein langsamer Drift-Check. Bei Abweichung (>60 Steps) erfolgt eine 10-RPM-Feinjustierung.
2. **Inertia-Check**: Oszillation +/- 360 Grad mit steigender Beschleunigung bis 40000 sps².

---

## 4. Hardware-Status
- GPIO 15: Validiert (Pinzetten-Test & NPN-Sensor ok).
- Pin-Belegung: Blau=GND, Schwarz=SIG, Rot=VCC.

---

## 5. Git & Workflow Preferences
- Keine Begriffe wie "final" oder "fertig" verwenden.
- Nach jeder Änderung: Dokumentation aktualisieren und Git-Commit durchführen.
