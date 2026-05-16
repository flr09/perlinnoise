# 📌 AGENT COORDINATION HUB

## 🕒 Aktueller Status (LIVE)
- **Stand:** 2026-05-16
- **Branch:** `v4-modular`
- **Firmware:** **v4.4.28** auf `perlin-v4.intern.gaengeviertel.de` (192.168.193.22) — **Phase 10.1 ✅** (Stabilität: Bugs 46/47/48/49/50/51/53), **Phase 10.2 ✅** (Silent-Mode: Bug 55 `silentCurrentMA`-Matrix), **Phase 10.3 ✅** (GUI-Härtung: Bugs 67/69/70/71/76/77/78/79/80). Player überlebt jetzt Slider-Drag, /preview-Stress, Mode-Switches; WD MS-invariant.
- **NVS-Schemata:** calib **4005** (Schema-4004→4005-Migration in `Storage_Calib::load` aktiv, `silentCurrentMA[5]` für MS=[16,32,64,128,256]), rtconf 4202.
- **Watchdog:** v1 mit adaptivem Fenster `max(3 s, 2/f)`, `WD_MIN_F_HZ=1.0`, MS-invariantem `WD_MIN_AMP_DEG=28°`.

### Lessons aus Phase 10.3 (v4.4.23 → v4.4.28)

- **WD MS-Invarianz (Bug 79):** Silent-Matrix wechselte Player auf MS=128 → `stepsPerRev=25600` → demandedHalfAmp=1849 bei 26°-Range → WD aktiv trotz Bewegung unter Sensor-Hysterese. Fix: Schwellwert in Grad statt Steps.
- **/preview-Heap-Churn (Bug 76):** Pattern-Familie 46/56 — String-Build + Response-Copy unter 10-Hz-Polling = ~80 KB/s. Streaming via `AsyncResponseStream` löst alle drei Vorkommen einheitlich.
- **Logger-Mutex-Falle (Bug 77):** Bug-74-Fix migrierte Logger irrtümlich auf `uartMutex`, der eigentlich TMC-Kommandos schützt. Folge: Status-Polling blockierte TMC-Operationen. Eigener `loggerMutex` entkoppelt.
- **DOM-Stabilität (Bug 78):** `innerHTML`-Rebuild pro 100 ms zerstörte + rebaute Buttons → Click-Race (Bug-58/63-Familie). `buildMotors()` einmal, dann nur `textContent`/`className` — keine Click-Verluste mehr.
- **setPower-Race-Iteration-3 (Bug 69):** Erst entfernt → Freeze, dann blind reingesetzt → Race, jetzt bedingt (`if currentMA==0`) → Freeze-Schutz für Erst-Power, Race weg bei Re-Power.
- **Slider-Flood (Bug 80):** `oninput→/set` pro Pixel = ~200 HTTP-Requests pro Drag, saturierte AsyncWebServer. `oninput→lokal, onchange→Server` reduziert auf 1 pro Drag.

### Lessons aus Phase 9 (v4.3.0 → v4.3.5)

- **Der 11-Hz-Drift-Trugschluss (v4.3.2):** Initial wurde ein Cutoff von fc=11 Hz gemessen. Die Analyse ergab, dass asymmetrischer Drift während des Sweeps den Motor aus dem Sensor-Sichtfeld schob.
- **FS-Drift-Fix & 31 Hz Breakthrough (v4.3.3):** Einführung von `fsResyncToEdge` zwischen den Frequenzbändern. Ergebnis: Der Z-Tacho ist mechanisch bis **31 Hz** (Hysterese-Floor) verlässlich. Der Drift hatte das Ergebnis zuvor um Faktor 3 verzerrt.
- **Bisektion-Resync (v4.3.4):** Frequenzen bis 50 Hz werden nun sauber als Hysterese-Floor (Blindflug) klassifiziert, ohne false-positive Stalls.
- **Player-Watchdog v1 (v4.3.5):** Cross-Check zwischen Tacho-Pulsen und Bewegungserwartung (Synthesis-Rate). 27s Smoke-Test erfolgreich.

### Nächste Schritte (Post-Phase 9)
- **Bug 36:** /log-Endpoint fixen (HTTP 500) — ✅ **v4.3.6** (2026-05-12, Hardware-verifiziert). Gemini-Commit `58b5463` (Tag `v4.4.0-rc1`) hatte den Endpoint zwar angelegt + `getBuffer()` eingeführt, aber das Lifetime-Problem (Temporary an `beginResponse`) blieb → Bug bestand weiter. v4.3.6 löst das mit lokaler String-Kopie analog `/status`-Pattern.
- **Bug 40:** Bisektions-Edge-Case bei f=26 Hz untersuchen.
- **Watchdog v2:** Auto-Recovery via `Op::pending.home` implementieren.
- **Stall-Verifikation:** Manueller Last-Test am Motor zur Validierung der SG-Fusion.

## 🎯 Auftrag v4

Modulare Endversion: künstlerische Bewegungs-Synthese aus V1 + Engineering-Reife aus v3, sauber zerlegt in 8 Schichten. Vollständige Spezifikation in [`v4/docs/FSD.md`](v4/docs/FSD.md).

## 🧱 Layer-Struktur

```
L7 Web-API + UI               (HTTP, JSON, HTML)
L6 Telemetrie + Sicherheit    (Watchdog, OpState, EmergencyStop)
L5 Programme (a Char + b Synth)
L4 Mechanik                   (Cal, Home, SetZero)
L3 Treiber                    (TMC, Stepper, Units, Motion)
L2 Persistierung              (NVS)
L1 HAL                        (Pins, ISR, PCNT, Sensor)
L0 Plattform                  (Boot, Tasks, Sync, Log)
```

**Regel:** Eine Schicht darf nur darunterliegende Schichten nutzen.

## 📋 Phasen-Plan (Kurzform)

| # | Inhalt | Status |
|---|---|---|
| 0 | Skelett + FSD | ✅ |
| 1 | L0+L1+L2+L3 + Minimal-L7 lauffähig | ✅ |
| 2 | L4 + Multi-Motor (X/Y/Z mit Sensorik, E open-loop) | ✅ |
| 3 | L5a Charakterisierung (v3-Tests + 3 Field-Weakening-Tests) | ✅ (Vorführung 7/7 grün, Z-Motor) |
| 4 | L5b Bewegungs-Synthese (V1-Funktionalität zurück) | ✅ Engine läuft, UI-Bindung offen |
| 5 | L6 Watchdog + Profile (aus v4_iteration1 integrieren) | ✅ |
| 6 | L7 Voll-UI (Lab + Performance + Telemetry-Viewer) | ✅ Layout, **GUI-Bindung kaputt** ⚠️ |
| 7 | L7 GUI-Reaktivierung (Bounds, /set-Echo, /preview, Canvas) | ✅ v4.1.0–4.1.3 |
| 8 | Calib-Skip via Zungenbreite + FreqSweep v2 | ✅ |
| 9 | TCO-Diagnostik + Watchdog v1 (31 Hz Breakthrough) | ✅ v4.3.0–v4.3.5 |
| 10 | GUI v4.4.0 "Performance Instrument" (2D-Spatial) | ⏳ geplant |

## 🎹 Phase 10 — GUI v4.4.0 "Performance Instrument"

**Ziel:** Transformation vom reinen Config-Editor zum intuitiven Instrument. Kontextsensitive Regler, räumliches Motor-Modell, Visualisierung + Recorder.

**Designentscheidungen (2026-05-12):**
1. **`rt.speed` (0–1):** Die UI rechnet kontextsensitiv (Wave: speed × cutoff Hz, Noise: Direkt-Slider).
2. **`moveType 8` (Coordinate):** Statisches Posing mit +/- Buttons (0.5°-Schritte). Snapshots in 8 NVS-Slots.
3. **Recorder:** RAM-Ringbuffer (60s ≈ 19 KB). Endpoints `/telemetry/start|stop|download`.
4. **Z-Fallback:** `tachoCutoffHz == 0` erbt Z-Wert (31 Hz).
5. **NVS-Only:** 8 Slots (v4.1.10) als einzige Preset-Quelle.

**Fahrplan (Implementierung durch Claude):**

| Schritt | Inhalt | Ziel | Status |
|---|---|---|---|
| **A** | NVS-Schema 4200→4201: `Point` in `Types.h`, `Point offsets[4]` in `RuntimeConfig`, Storage_Runtime erweitert, Defaults 2×2-Grid | v4.4.0-rc1 | ✅ 2026-05-12 |
| **B** | L7-API: `/bounds` liefert `tachoCutoffHz` raw+eff (Z-Fallback) + `offsets`. `/set?ofx0..3=…&ofy0..3=…`. posDeg-Edit folgt mit C. | v4.4.1 | ✅ 2026-05-12 |
| **C** | L5b: `moveType 8` Coordinate (statisch via `posDeg[4]`). Noise sampelt pro Motor an `(flight + offset·mspace)`. NVS 4201→4202. | v4.4.2 | ✅ 2026-05-13 |
| **D** | Player responsive: Media-Query `<600 px` versteckt Motor/Spatial/Log-Sektionen, Slider+Start+Presets+REC bleiben. | v4.4.7 | ✅ 2026-05-13 |
| **E** | Player-UI Kern: kontextsensitive Slider-Labels + Live-Werte (Hz/°/%/cm) + Canvas-Dot-Overlay an (x,y) mit posDeg-Helligkeit + Coordinate-Option im Dropdown | v4.4.3 | ✅ 2026-05-13 |
| **F** | 2D-Kompass-SVG pro Motor: Click/Drag → Offsets, ±0.5°-Buttons → posDeg | v4.4.4 | ✅ 2026-05-13 |
| **G** | Recorder L6-Block + `/rec/start|stop|state|csv` + REC-Button im Player. Pfad-History → Backlog. | v4.4.5 | ✅ 2026-05-13 |
| **H** | NVS-Preset-System in Player-UI: 8 Slot-Buttons + Save-Mode-Toggle. Backend bestand seit v4.1.10. | v4.4.6 | ✅ 2026-05-13 |
| **I** | ~~Chart.js lokal~~ — deferred (200 KB zu groß für PROGMEM, keine Live-Charts in Lab) | — | ⏸ |
| **J** | Integrationstest, Bug-Sweep, Tag `v4.4.0` | v4.4.0 | ⚪ |

### 1. Kontextsensitive Regler (Labels & Units)
... (Rest der Details bleibt erhalten, wird aber durch Claude umgesetzt)
Basierend auf `moveType` müssen Fader ihre Beschriftung und Einheit ändern:
- **Wellenform (Sine/Square/Saw):**
    - `speed` → **Frequenz [Hz]** (Slider-Max dynamisch aus `cal.tachoCutoffHz`).
    - `contrast` → **Amplitude [°]** (0 bis `cal.rangeDeg`).
    - `mspace` → **Phasenversatz [°/Motor]** (0–360°).
    - `zShape` → **Duty Cycle [%]** (Square) oder **Steigung** (Saw).
- **Noise (Linear/Figure8):**
    - `speed` → **Flug-Tempo**.
    - `contrast` → **Kontrast / Intensität**.
    - `mspace` → **Motor-Abstand [cm]**.

### 2. Räumliches Modell & Waypoints (2D-Kompass)
Die Motoren sitzen nicht mehr auf einer Linie.
- **Daten:** `RuntimeConfig` bekommt `Point offsets[4] {float x, y}` (NVS Schema 4005).
- **Coordinate Mode (`moveType 8`):** 
    - Ermöglicht das Anfahren starrer Winkel pro Motor ("Bild" oder "Waypoint").
    - **Fine-Tuning:** Jedes der 4 Motor-Displays bekommt kleine **+/- Buttons**, um den Winkel (posDeg) präzise zu justieren (z.B. 0.5° Schritte).
    - Diese "Skulptur-Haltung" kann als Step in einen Chase/Preset gespeichert werden.
- **UI:** Ein kleiner **2D-Kompass/Grid** pro Motor für (x,y) Offsets.
- **Logik:** `Synthesis.cpp` sampelt den Noise an diesen (x,y) Offsets relativ zum Kamerapfad (`flightX/Y`).

### 3. Visualisierung & Recorder (Parity mit "Record"-Version)
Wiederherstellung der visuellen Intelligenz aus `perlin_visualizer.html`:
- **Dots (M1–M4):** Farbige Punkte auf dem Canvas, die an ihren räumlichen (x,y) Koordinaten tanzen.
- **Hub-Feedback:** Helligkeit/Größe der Dots zeigt die aktuelle Auslenkung (`posDeg`) in Echtzeit.
- **Path History (Recorder):** 
    - Die "grüne Linie" (vergangener Flugpfad) und "rote Linie" (Vorschau) muss zurück.
    - **Session-Recording:** Ein "REC"-Button, der die Telemetrie-Aufzeichnung auf dem ESP32 startet/stoppt (via `/telemetry`).
    - **Local History:** Speichern von Slider-Snapshots in den 8 Slots (LocalStorage).

### 4. Code-Reuse & Bibliotheken
- **Basis:** `perlin_visualizer.html` (Pfad-Logik & Motor-Offsets).
- **Chart.js (v4.x):** Für die Echtzeit-Graphen im "Lab"-Bereich (muss als lokale Datei/String eingebettet sein für Offline-Betrieb).
- **Simplex JS:** Vorhanden in `perlin_visualizer.html` (Zeilen 150–180).
- **Compass-UI:** Nutze **Vanilla SVG** für das 2D-Grid der Motor-Positionen (keine externen Libs nötig).

### 5. Architektur-Split
- **Player Page:** "Performance Instrument" (Fokus auf Flow, ästhetische Visualisierung, Recorder).
- **Lab Page:** Engineering, fc-Diagnose-Diagramme, NVS-Details, Bug-Log.

## 🛠️ Wichtige Erkenntnisse (Shared Knowledge)

- **E-Motor hat keinen Endstop-Pin** → läuft open-loop, nur `/setzero` als Nullpunkt. Cal/Home nur für X/Y/Z.
- **Sensorik:** LJ12A3-4-Z/BX (induktiv, NPN NO) — AS5600 evtl. später für Folgeprojekt.
- **Hardware-Vorbehalt:** Aktuell nur Z mit montiertem Sensor (mechanisch). Y/X/E warten auf neue Konstruktion.
- **GPIO 15** möglicherweise vorbeschädigt durch PNP-Vorfall am 2026-03-24 — bei Sensor-Problemen im Hinterkopf behalten.
- **Field Weakening** bei TMC2209 nicht direkt möglich (kein FOC). Drei verwandte Test-Programme in L5a geplant: `Prog_FullstepSwitch`, `Prog_PhaseLead`, `Prog_CurrentSweepHiRPM`.
- **WiFi-PW** war in v3 als Klartext in `wifi_settings.h` — in v4 wird das in NVS via `Storage_Wifi` migriert.
