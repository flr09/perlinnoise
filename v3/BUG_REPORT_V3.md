# Technischer Statusbericht & Bug-Report

**Firmware:** v3.6.2 | **Stand:** 2026-03-26
**Letzte verifiziert lauffähige FW:** v3.5.4 (`parcour_2026-03-25T21-59-29.csv`, 0 Stalls, 200–2500 RPM Ziel)

---

## 1. Historische Bugs (v3.3.x — Baseline)

| ID | FW | Beschreibung | Fix |
|----|----|--------------|-----|
| **BUG-11** | 3.3.x | UART-Blocking in Motor-Loop | UART-Reads in TelemCache auf Core 0 ausgelagert |
| **BUG-12** | 3.3.x | AccelStepper-Limit >15.000 sps | Migration auf FastAccelStepper |
| **BUG-14** | 3.3.x | Tacho-Polling statt ISR | Hardware-Interrupt auf TACHO_PIN (GPIO 15) |
| **BUG-15** | 3.3.5 | vTaskDelay(1) begrenzte auf 1000 sps | Delay entfernt |
| **BUG-16** | 3.3.7 | GPIO 25 Enable-Logik invertiert → Motor stromlos | Invertiert in v3.3.8 |

---

## 2. FastAccelStepper-Migration v3.5.x

| ID | FW | Schwere | Beschreibung | Fix |
|----|----|---------|--------------|----|
| **A1** | 3.5.2 | 🔴 Crash | `tachoISR` rief `stepper->getCurrentPosition()` — nicht ISR-sicher | Nur `pulseCount++` |
| **A2** | 3.5.2 | 🟠 Deadlock | `updateTelemCache` Mutex-Timeout 10ms, `applyDriverSettings` hält bis 16ms | 100ms |
| **A3** | 3.5.2 | 🟠 Jitter | `learnSGProfile` las `driverX.SG_RESULT()` direkt von Core 1 | Auf `tCache.sg` umgestellt |
| **B1** | 3.5.3 | 🔴 Boot-Crash | `xSemaphoreCreateMutex()` global — FreeRTOS-Heap bei globalem Konstruktor nicht bereit | In `initMotors()` verschoben |
| **B2** | 3.5.3 | 🟠 Silent fail | `learnSGProfile` kein `stopMove()` → `setMicrosteps(64)` übersprungen | `stopMove()` vor setMicrosteps |
| **B3** | 3.5.3 | 🟠 Silent fail | `runCoastTest` kein `stopMove()` → Motor lief endlos | `stopMove()` am Ende |
| **D1** | 3.5.4 | 🔴 Falsche Funktion | `characterizeSensor`: kein `runForward()`/`runBackward()` nach `setSpeedInHz(200)` → falscher `triggerCenter` | Explizite Richtungsbefehle eingefügt |
| **D2** | 3.5.4 | 🟠 Jitter | `learnSGProfile`: `runForward()` in jeder Messschleifen-Iteration | `runForward()` vor die Schleife verschoben |

---

## 3. Parcour-Messfehler v3.6.0

| ID | FW | Schwere | Beschreibung | Fix |
|----|----|---------|--------------|----|
| **E1** | 3.6.0 | 🔴 Falsche Messung | `runSpeedTest` hat kein `setAcceleration()` aufgerufen → erbt 2000 sps² von homeMotor. Motor erreichte nur 26–37% der Zielgeschwindigkeit. | `setAcceleration(30000)` am Anfang von `runSpeedTest` |
| **E2** | 3.6.0 | 🔴 Falsche Messung | Settle = immer 500ms, unabhängig ob Zielgeschwindigkeit erreicht | Aktives Warten auf `getCurrentSpeedInMilliHz() >= 95%` mit Timeout |
| **E3** | 3.6.0 | 🟠 Falsche Diagnose | Boost-Logik feuerte bei jedem Schritt wegen E1/E2, trieb Strom unnötig auf 900mA | Boost prüft jetzt `reachedRpm` aus `getCurrentSpeedInMilliHz()` |

---

## 4. PCNT-Integration v3.6.1/v3.6.2

| ID | FW | Schwere | Beschreibung | Fix |
|----|----|---------|--------------|----|
| **F1** | 3.6.1 | 🔴 System hängt | `pcnt_unit_config()` setzt GPIO 27 (X_STEP) intern auf Input → FastAccelStepper RMT-Output blockiert → Motor dreht sich nicht | `gpio_set_direction(GPIO_MODE_INPUT_OUTPUT)` nach `pcnt_unit_config()` in v3.6.2 |

---

## 5. Bekannte Schwachstellen (offen, noch nicht behoben)

| Thema | Beschreibung | Priorität |
|-------|--------------|-----------|
| **characterizeSensor — kein 360°-Scan** | Suchfahrt startet blind. Bei ungünstiger Startposition braucht der Motor 2+ Umdrehungen zur Sensor-Suche | Mittel |
| **characterizeSensor — Geschwindigkeit** | 200/400 sps willkürlich gewählt. Bei 400 sps ist der Yield()-Fehler noch messbar (jetzt mit PCNT behoben in v3.6.2) | Behoben in v3.6.2 |
| **runSpeedTest — kein 10er-Feinraster** | Bei Ausfall nahe der Grenzgeschwindigkeit wird aktuell Strom erhöht statt in 10-RPM-Schritten zu suchen. `PARCOUR_RPM_FINE` ist definiert aber ungenutzt | Niedrig |
| **SG bei niedrigen RPM** | SG_RESULT bei < 700 RPM (< 12000 sps) = 0–10. Für NEMA14 Pancake (1,29 mH) zu schwaches Signal für zuverlässige Stall-Erkennung | Informativ |

---

## 6. Versionsübersicht

| Version | Commit | Status | Beschreibung |
|---------|--------|--------|--------------|
| 3.3.3 | — | Archiv | Jitter-Audit dokumentiert |
| 3.4.0 | `54180c3` | Archiv | Logic overhaul, measureActualRpm |
| 3.5.1 | `8307f17` | Archiv | FastAccelStepper API-Korrekturen |
| 3.5.2 | `61b0e50` | Archiv | ISR/Mutex/UART-Fixes + Winkelmarker |
| 3.5.3 | `566b3e6` | Archiv | Boot-Crash + 4 Logic-Bugs |
| **3.5.4** | `97f0dd1` | ✅ **Verifiziert lauffähig** | D1/D2 + UI Bedienreihenfolge. Lief 2500 RPM Ziel, 0 Stalls |
| 3.6.0 | `3ddcc85` | ⚠️ Nicht getestet | E1/E2/E3: Beschleunigung + Settle-Fix |
| 3.6.1 | `e2bc638` | 🔴 Defekt | PCNT eingefügt — GPIO-Konflikt → System hängt |
| **3.6.2** | `cc9b0d9` | 🟡 Gebaut, nicht getestet | F1: GPIO_MODE_INPUT_OUTPUT Fix |
