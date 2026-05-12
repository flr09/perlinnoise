# 🔴 Project PerlinNoise v4 — Bug Log

Legend: 🔴 bug | 🟡 minor/refactor | ✅ fixed | 🔵 discovery | ⏳ awaiting verification

Quellen: dieser Eintrag · `AGENT_COORDINATION.md` Lessons · `v4/docs/FSD.md` §9.

| ID | Date | Type | Title | Status |
|---|---|---|---|---|
| 1 | 2026-05-04 | 🔴 | Calibration-Skip Logic Error (Decel-Bias) | ✅ v4.1.3-rc2 + v4.1.4 (Self-Consistency) verifiziert |
| 2 | 2026-05-02 | 🔴 | ElegantOTA Reboot missing loop() | ✅ v4.1.3 ⏳ verify |
| 3 | 2026-05-04 | 🟡 | NoiseEngine Redundancy (Synthesis.cpp duplication) | ✅ v4.1.5 (NoiseEngine.noise() Passthrough, sn-Instanz konsolidiert) |
| 4 | 2026-05-04 | 🟡 | Tacho ISR Dead Code (Polling preferred) | ✅ v4.1.5 (ISR-Templates + void-casts entfernt) |
| 5 | 2026-05-04 | 🔵 | Z-Motor Sweet-Spot > Hard-Limit (1000mA vs 900mA) | 🔵 noted |
| 6 | 2026-04-29 | 🔴 | Tacho-Präzision: ms-Auflösung erzeugt spurious Stalls bei >1000 RPM | ✅ v4.0.1/4.0.2 (`micros()`-Migration, NVS 4001→4002) |
| 7 | 2026-04-29 | 🔴 | SynthesisTask blockierte Movement während Engineering-Tests | ✅ v4 (decoupled SynthesisTask Core 1, Prio 2) |
| 8 | 2026-04-29 | 🔴 | FreqSweep um Mitte → 0 Pulse bei kleinen Amplituden | ✅ v4 (Schwinge um `triggerStartDeg` statt Mitte) |
| 9 | 2026-04-29 | 🔴 | Stall im Test-Parcours zerstörte Home-Position für Folgetests | ✅ v4 (auto `Homing::run()` nach detektiertem Stall) |
| 10 | 2026-04-30 | 🔴 | Calib-Bug 3a: P1+P2 mit `stopMove` dazwischen → Decel-Overshoot landet außerhalb Zunge → false-positive HIGH | ✅ v4 (P1+P2 nahtlos, durchgehende CW-Bewegung) |
| 11 | 2026-04-30 | 🔴 | Calib-Bug 3b: EdgeTouch backOff < Zungenbreite → false-positive Sensor-LOW beim Re-Anfahrt | ✅ v4 (`backOff` 0.05 → 0.15 rev) + maxDelta-Guard |
| 12 | 2026-04-30 | 🔴 | Calib-Bug 3c: EdgeTouch return 0 vs. negative Position → ambig „kein Touch" | ✅ v4 (`LONG_MIN`-Sentinel statt 0) |
| 13 | 2026-04-29 | 🔴 | ArduinoOTA bei Netz-Timeouts instabil — kein Browser-Fallback | ✅ v4.0.2 (ElegantOTA `/update` Auth admin/12345678) |
| 14 | 2026-04-30 | 🔴 | Phase 6 GUI: Slider-`max` hardcoded statt aus NVS-Bounds → User dreht Engine über gemessene Grenzen | ✅ v4.1.0 (`/bounds`-Endpoint + Engine-Cap in `Synthesis::start`) |
| 15 | 2026-04-30 | 🔴 | Phase 6 GUI: `/set` antwortet `"OK"` plain → JS kann Slider nach POST nicht refreshen | ✅ v4.1.1 (`/set` JSON-Echo des `v4::rt`-State) |
| 16 | 2026-04-30 | 🔴 | Phase 6 GUI: kein `/preview`-Endpoint → Canvas zeigt lokale Simplex-Animation, ignoriert Engine | ✅ v4.1.2 (`Synthesis::getPreviewBytes()` + `/preview`) |
| 17 | 2026-04-30 | 🔴 | Phase 6 GUI: Wellenform-Wechsel (moveType 3–5) nicht im Canvas sichtbar | ✅ v4.1.3 (Canvas pollt `/preview` 10 Hz, mode-switch via `j.mode`) |
| 18 | 2026-04-29 | 🔴 | FreqSweep „Show 7/7" lieferte nur 7 Impulse, fuhr nicht in Stall — Hyperbel-Beziehung amp×f² nicht modelliert | ✅ v4.2.2 (retake mit feinem 6..50 Hz Raster, 2 echte Datenpunkte 6+8 Hz) |
| 21 | 2026-05-05 | 🔴 | FreqSweep v2: Re-Home-Race nach Stall bei kleiner amp → EdgeTouch miss → Folgetests an Müll-Position | ✅ v4.2.2 (Re-Home dank ID-28-Fix robust, Hysterese-Pfad triggert kein Re-Home) |
| 22 | 2026-05-05 | 🔴 | FreqSweep v2 Stall-Detektor: amp < Sensor-Hysterese erzeugt 0 Pulse, fälschlich als Stall klassifiziert → Bisektion läuft in Floor (stallAmp=5 für mehrere Bänder) | ✅ v4.2.2 (NO-PULSES wird jetzt als Hysterese-Floor klassifiziert, returns 0, kein false-Stall) |
| 32 | 2026-05-06 | 🔵 | **Sensor-Hysterese auf Z-Mechanik bei ~9 Hz**: bei amp ≥ 13.7° (8 Hz) noch 2/4 Pulse, bei amp ≤ 8.8° (10 Hz) keine Pulse mehr. Tacho-basierte Reversal-Charakterisierung damit nur für 6+8 Hz Bänder möglich. | 🔵 noted (Hardware-Limit, mechanisch verbesserbar) |
| 33 | 2026-05-06 | 🔴 | TMC2209 StallGuard liefert bei oszillierender Bewegung konstant `SG_RESULT=0` selbst bei nachweislicher Motor-Bewegung (Tacho zeigt Pulse). Ursache: SG braucht continuous spinning über TPWMTHRS-Schwelle, Reversal-Halbperioden 50 ms zu kurz. Plus Telemetry-Poll 100 ms verpasst Spitzen. | ✅ v4.3.2 Hardware-final-bestätigt: TCO-Lauf zeigte sg=0 in ALLEN 10 Datenpunkten (5..14 Hz). SG-Pfad endgültig als untauglich für Reversal eingestuft → Phase B verworfen, Phase C ($1/f^2$-Extrapolation) vorgezogen. SG4 bleibt Folgeprojekt v5 (TMC5160+FOC). |
| 36 | 2026-05-09 | 🔴 | /log-Endpoint liefert HTTP 500 (Async-Race?) | ✅ v4.3.6 (lokale String-Kopie analog /status) |
| 37 | 2026-05-09 | 🔴 | fc-Messung verzerrt durch asymmetrischen Drift | ✅ v4.3.3 (FS-Drift-Fix) |
| 38 | 2026-05-09 | 🔴 | Bisektion verliert Sync bei f > 30 Hz | ✅ v4.3.4 (Internal Re-Sync) |
| 39 | 2026-05-09 | 🟡 | Watchdog v1: 100ms Telemetry Poll zu grob für Stall-Trigger | ✅ v4.3.5 (3s Fenster mit ratio-Kriterium) |
| 40 | 2026-05-09 | 🟡 | Bisektions-Edge-Case bei f=26 Hz (kosmetisch) | ⏳ backlog |
| 41 | 2026-05-09 | 🔵 | **Hardware-Limit Z-Tacho**: fc=31 Hz verifiziert. | 🔵 noted |
| 23 | 2026-05-05 | 🟡 | Missing Persistence: Slider/Presets/WiFi verlieren Werte nach Reboot bzw. hardcoded | ✅ v4.1.9 (Config 5 s Debounce) + v4.1.10 (8 Preset-Slots NVS) + v4.2.0 (WiFi NVS + /wifisave nicht-blockierend) |
| 35 | 2026-05-09 | 🟡 | TCO Drift-Indikator: `drift = pos_end − edge_pos` zeigt nur die Schwingungs-Endpunkt-Asymmetrie (drift = ±amp je nach swings-Parität), nicht echten Step-Loss. Ehrlich wäre `effDrift = drift mod (2·amp)`. | 🟡 noted für Phase-A-v2; in v4.3.2 funktional ausreichend, weil pulses bereits primärer Indikator + sprQuarter-Notausschalter |
| 36 | 2026-05-09 | 🔴 | `/log`-Endpoint wirft HTTP 500 (auf v4.3.2 reproduziert). Workaround: `/status` enthält `log`-Feld. Ursache: Temporary aus `Logger::getBuffer()` direkt an `beginResponse` übergeben — Lifetime nicht garantiert bis zum Async-Send. | ✅ v4.3.6 (lokale `String log = Logger::getBuffer()` analog zu `/status`-Pattern Z. 716–722, Empty-Edge-Case mit `"(empty)\n"` abgefangen). Hardware-verifiziert: HTTP 200, 434 B, 40 ms. |
| 37 | 2026-05-09 | 🔴 | **FS2/TCO „läuft im Sweep nach links raus":** zwischen den Bändern fehlte Re-Sync auf physische Sensor-Kante. Step-Counter bleibt logisch bei `edgePos`, physisch driftet Motor durch Hysterese-Schwingung kumulativ in eine Richtung. Folge: nachfolgende Bänder schwingen um falsche Position, Tacho-Pulse bleiben aus → künstlich niedriger fcutoff. | ✅ v4.3.3 (`fsResyncToEdge` Helper: EdgeTouch CW→LOW = `triggerStartDeg` zwischen jedem Band, Homing-Fallback bei Miss). Hardware-Verifikation Z-Motor: fcutoff 11 Hz → **31 Hz** mit Re-Sync — Drift hatte ~3× Verzerrung verursacht. |
| 38 | 2026-05-09 | 🔴 | TCO-Re-Sync v1 (rc1) nutzte fälschlich `EdgeTouch::touch(LOW=HIGH, +1)` → suchte CW-Austrittskante (`triggerEndDeg`) statt CW-Eintrittskante (`triggerStartDeg`). Folge: edgePos um Zungenbreite (~30°) versetzt, fcutoff 11→7 Hz statt 11→31. | ✅ v4.3.3-rc2 (target=`LOW` für HIGH→LOW-Übergang) |
| 39 | 2026-05-09 | 🔴 | FS2-Bisektion-internal Re-Home miss-fired bei f=26..50. Alter Pfad: `Homing::run()` + `EdgeTouch(150 sps, 1 Sample)` zur Kante — nach mehreren Bisektions-Stufen (amp 23,12,6,3) miss-fired EdgeTouch. Folge: Bisektion lief in `stallAmp=1` (Floor) für f=36, 50 trotz reiner Hysterese. | ✅ v4.3.4 (`fsResyncToEdge` auch im Bisektions-`else`-Branch). Hardware-Verifikation: f=36, 50 jetzt sauber als NO-PULSES/Hysterese-Floor klassifiziert (stallAmp=0), keine EdgeTouch-Misses mehr. |
| 40 | 2026-05-09 | 🟡 | f=26 Edge-Case: bei amp=46 noch 5/12, ab amp=23 nur 1/12, dann 0/12. Bisektion landet auf stallAmp=1. Möglich: zwischen den engen Bisektions-Stufen kumuliert sich Drift schneller als der Re-Sync sie auflöst. | 🟡 Backlog. Workaround-Idee für späteren v4: Bisektion mit „lo<latest_valid_amp"-Floor abbrechen, sobald 2× pulses=0 in Folge. |
| 41 | 2026-05-09 | 🟣 | **Player-Watchdog v1 (v4.3.5):** Synthesis::tickWatchdog vergleicht reale Tacho-Pulse-Rate gegen erwartete (2·f) im 3-s-Fenster. Bei ratio < 50 % → Synth-Stop + Log. Aktiv nur Wave-Mode mit demanded Halb-Amp > 250 Steps und 0.3 Hz < f < tachoCutoffHz. v1 ohne Auto-Recovery — User reagiert manuell. | ✅ v4.3.5 (Smoke-Test 15 s Sinus + 12 s Square rasant, kein false-positive). Echter Stall-Test offen (User muss Motor mechanisch behindern). |
| 42 | 2026-05-11 | 🔴 | **User Feedback v4.3.5:** System "nicht gut zu bedienen", "ziemlich laut", "Geschwindigkeit max führt zu Error" (Watchdog?). Bauhaus-Design fehlt in FW. | 🔴 noted, Ziel für Phase 10 |
| 43 | 2026-05-12 | 🔴 | **Calib P1+P2 ohne Stall-Schutz** — User-Report 2026-05-12: Motor fuhr nur ~80° physisch, Calib loggte „P1 Eintritt nicht gefunden nach ~15360 steps". Ursache: `Calibration.cpp:91-116` (Phase 1+2 Grob-CW) hat keinerlei Live-Stall-Detection. FastAccelStepper zählt Microsteps brav weiter, auch wenn der Rotor mechanisch stalled (Strom zu niedrig, Decel-Spike, mechanisch gehemmt, TPWMTHRS-Race direkt nach Boot). Watchdog (v4.3.5) ist Wave-Mode-only, SG ist auf Reversal verworfen — unidirektionaler Spin in Calib hätte SG nutzen können, ist aber nicht verdrahtet. Symptom: User sieht Motor stillstehen, Log meldet hohe `traveled`-Werte → klassische Microstep-vs-Realdrehung-Divergenz. | 🔴 Backlog. Kandidaten-Fixes: (a) SG-Monitor im P1+P2 (TMC2209-`SG_RESULT` < threshold → Abbruch mit „STALL"-Log, nicht „nicht gefunden"); (b) Tacho-basierter Sanity-Check (RPM≈0 trotz Stepper-Pulses → Stall); (c) bei `learnedCurrentMA=0` Default-RunCurrent ausreichend hoch wählen. Erst Phase 10 fertig, dann adressieren. |
| 24 | 2026-05-05 | 🔴 | Square/Saw springt zu langsam → wirkt sinusförmig (`DEFAULT_ACC_NOISE` 4000 zu niedrig für Wave-Modi) | ✅ v4.1.7 (nutzt `maxAccel` bis `HARD_ACCEL_CAP=500000`) |
| 25 | 2026-05-05 | 🔴 | EdgeC-Slider wird in `Synthesis::tick()` ignoriations (nur `zShape` + `contrast` angewendet) | ✅ v4.1.7 (applyShape nutzt `edgeC` für Motoren + Preview) |
| 26 | 2026-05-05 | 🟡 | HAL Totholz: `v4::rt.fan/lamp` werden gesetzt aber nirgends an GPIO ausgegeben (PWM Fan GPIO 13, Lamp GPIO 2) | ✅ v4.1.6 (`Hal_Output` mit PWM-Fan + discrete Lamp, throttled in tick()) |
| 27 | 2026-05-05 | 🔴 | **Gemini v4.2.0 Sammel-Commit kritisch defekt:** `WebServer::begin` umbenannt zu `init` ohne Header/main-Update → Linker greift auf Arduino-Lib `WebServer::begin` → unsere Endpoints nie registriert. Plus NVS-Wear-Out durch `save()` bei jedem /set, plus 1 M sps² Acc-Cap, plus blocking delay() im /wifisave. | ✅ v4.1.5 reset (Commit `8c0f389` verworfen, neu aufgeteilt v4.1.6–v4.2.0 mit Hardware-Test pro Schritt) |
| 28 | 2026-05-05 | 🔴 | Homing nach Stall findet Sensor nicht: Suchradius 2 rev nur CW reicht nicht, weil Step-Counter nach Stall vom physischen Stand abweicht — Zunge kann je nach Fall hinter dem Motor liegen. Zweiter Test (Inertia) klappt dann zufällig wenn Motor durch Drehung in die Zunge kommt. | ✅ v4.1.8 (`searchSensorOneDir()` als Helper, erst CW max 1.5 rev, bei Miss CCW max 1.5 rev → 3 rev Gesamt-Coverage) |
| 29 | 2026-05-05 | 🔴 | Wave-Mode-Geeier / Performance-Mismatch: Square/Saw nutzen Test-Werte nicht reaktiv | ✅ v4.2.3 (Reactive Caps in 4.2.1 + Wave-Cap aus FreqSweep-v2-Daten in 4.2.3 — `effSpeed = f * sqrt(maxAmpSafe/demanded)`-Cap, range bleibt unverändert) |
| 30 | 2026-05-06 | 🟡 | **Cal-Cache Optimization**: 100Hz loop las NVS statt Cache | ✅ v4.2.1 (calCache in Synthesis.cpp) |

---

### [ID 29] Performance-Mismatch & Mode-Reactivity
- **Problem:** Wenn die Engine läuft und der User den Modus von Noise auf Square umschaltet, blieb die Beschleunigung auf dem niedrigen Noise-Default (4000) hängen. `applyEngineCap` wurde nur in `start()` gerufen. Square wirkte "weich" und erreichte nie die rasanten Test-Werte.
- **Fix v4.2.1:** `tick()` erkennt Modus-Wechsel und aktualisiert `s->setAcceleration/setSpeedInHz` reaktiv. Square/Saw nutzen nun den vollen `maxAccel`-Wert aus der Charakterisierung (evidenzbasiert), während Noise ruhig bleibt.

### [ID 30] Cal-Cache (NVS Latency Fix)
- **Problem:** `applyEngineCap` lud bei jedem Aufruf die Grenzen aus dem NVS. In STEP-Mode passierte das 100x pro Sekunde — schlecht für Latenz und Flash-Wear.
- **Fix v4.2.1:** `Synthesis.cpp` hält einen `calCache[4]`, der nur beim Start oder Modus-Wechsel gefüllt wird.

### [ID 28] Homing nach Stall findet Sensor nicht
- **Symptom (Show 7/7 Hardware-Test 2026-05-05):** Nach SpeedTest-Stall @1600 RPM logt `HOME: Sensor nicht gefunden`. Inertia-Test direkt danach läuft erst, stallt selbst, ruft Re-Home — und das klappt dann („HOME: Kante bestätigen... HOME: @0° OK"). Nach Coast wieder „HOME: Sensor nicht gefunden". Sporadisch.
- **Root Cause:** `Homing::run()` machte nur eine Schnellsuche CW max 2 rev. Nach Stall ist der FastAccelStepper-Position-Counter desynchronisiert von der physischen Achse — wenn die Sensor-Zunge zufällig in CCW-Richtung relativ zur aktuellen Position liegt, fährt die CW-Suche an ihr vorbei. Der nächste Test fährt zufällig durch die Zunge, dann triggert das interne Stall-Re-Home und findet sie aus einer anderen Position.
- **Fix v4.1.8:** Helper `searchSensorOneDir(motorIdx, pin, dir, maxRev)` extrahiert. `Homing::run()` macht erst CW max 1.5 rev, bei Miss CCW max 1.5 rev. Insgesamt 3 rev Coverage — eine Sensor-Zunge MUSS innerhalb davon liegen (es gibt nur eine pro Motor). Logging „CW-Suche miss, versuche CCW..." sichtbar, falls beide fehlschlagen klare Meldung mit Δ.

### [ID 29] Wave-Mode-Geeier bei extremen Settings
- **Symptom (User-Beobachtung 2026-05-05):** Square-Wave (Modus 5) mit speed × range × dyn=rasant zeigt „Rumgeeier an den Endpunkten" statt sauberer Sprünge — als sei der PID nicht eingestellt. Sinus (Modus 3) wirkt sauber. Schließt rc2-Wave-Accel-Fix (ID 24) nicht aus, ist eine Schicht tiefer.
- **Root Cause (Physik):** Square braucht pro Halbperiode eine vollständige Reversal: `t_jump = 2·sqrt(distance/maxAcc)`. Mit Z-Motor-Cal (`maxAccel=500k`, range=300°·1.30=390°, distance=3470 steps) → t_jump ≈ 240 ms. Halbperiode bei speed≈8: 240 ms — gerade. Bei speed=10: 190 ms < t_jump → moveTo wird neu aufgerufen während Motor noch decel/accel läuft → Bewegung kollabiert um die Mitte.
- **Konzeptionelle Lücke:** Der Charakterisierungs-Parcours misst Vorwärts-Verhalten (SpeedTest, InertiaTest, KatapultTest). Reversal-Capability als Funktion von (Frequenz, Amplitude) wird nicht gemessen. FreqSweep wäre genau das gewesen — v2 ist aber wegen IDs 21+22 zurückgerollt.
- **Fix-Plan:** v4.2.1 = FreqSweep-v2-Retake (Hysterese-Floor pro Frequenz vorab bestimmen, dann Stall-Suche). v4.2.2 = Synthesis-Wave-Cap nutzt die in NVS gespeicherten Reversal-Limits, um effSpeed×effRange physikalisch zu cappen. Square läuft so weit wie der Motor wirklich kann, kein Geeier mehr.

### [ID 26] HAL Totholz (Fan/Lamp)
- **Problem:** `v4::rt.fan` (0..255) und `v4::rt.lamp` wurden in `/set` gesetzt, aber `Synthesis.cpp` reichte sie nirgends an die GPIO weiter. Lüfter & Lampe nie ansteuerbar.
- **Fix v4.1.6:** Neuer `HalOutput`-Block (L1) mit PWM-Fan (GPIO 13, LEDC-Channel 4, 5 kHz, 8 Bit) und discrete Lamp (GPIO 2). `Synthesis::init()` ruft `HalOutput::init()`. `Synthesis::tick()` ruft `pushOutputs()` mit Wert-Throttle: nur bei Änderung von `v4::rt.fan/lamp` wird `ledcWrite/digitalWrite` aufgerufen — keine 100 GPIO-Calls/s ohne Mehrwert. Funktioniert auch bei stehender Synthese (Push vor `if (!running) return`).

### [ID 27] Gemini v4.2.0 Sammel-Commit defekt
- **Befund:** Commit `8c0f389` ("v4.2.0: Hardware, Persistence, Performance, EdgeC") kompilierte SUCCESS, hätte aber als Firmware kaputt funktioniert:
  - **Kritisch:** `WebServer::begin()` in `.cpp` umbenannt zu `init()`, aber `WebServer.h` und `main_v4.cpp:133` nicht angepasst. Linker griff zur Arduino-Library `lib052/WebServer/WebServer.cpp` → unsere `init()` würde nie aufgerufen, alle Endpoints + ElegantOTA tot. Symbol-Check via `nm` bestätigte `_ZN9WebServer4initEv` (unsere) vs `_ZN9WebServer5beginEv` (Arduino-Lib).
  - **NVS-Wear-Out:** `StorageRuntime::save(c)` bei JEDEM /set-Call. Slider-Drag = ~50 Writes/s × 60 s = 3000 Writes/min. ESP32-NVS-Cells (~100 k Cycles) wären in <1 h verschlissen.
  - **Acc-Cap zu hoch:** `accCap = 1000000` (1 Mio sps²) für Wave-/STEP-Mode. Bei uncal-Motor (cal.valid=false) keine Heruntercappung → 250× Default → Mechanik-Schock.
  - **Blocking delay() im /wifisave-Handler:** `delay(100); ESP.restart()` blockiert AsyncWebServer-Task — Response kommt nicht zuverlässig durch.
  - **synth-Action zum Toggle gemacht** (war: nur start) — UI-Verhaltensänderung ohne Test.
- **Fix:** Hard-Reset auf v4.1.5, Aufteilung in 5 saubere Tags v4.1.6 → v4.2.0 mit Hardware-Test je Schritt. Bug-Log dokumentiert die Aufteilung.

### [ID 1] Calibration-Skip Logic Error (Decel-Bias)
- **Symptom:** Calib-Skip greift nie, auch bei unveränderter Mechanik. Zweiter Calib-Lauf gleich langsam wie der erste.
- **Root Cause:** In `v4/src/L4_mechanics/Calibration.cpp` rc1 wurde `p2Pos` nach `s->stopMove()` + `Motion::waitWhileRunning()` + `delay(80)` erfasst, `p1Pos` aber direkt beim Trigger. Bei 2000 sps + 15000 Decel ergibt `v²/(2a) ≈ 133` Steps Bremsweg, der systematisch nur in `p2Pos` einging. `relDelta = 133 / expectedWidth` lag immer weit über 5 %.
- **Fix (rc2):** `p2Pos = s->getCurrentPosition()` direkt in der Trigger-Schleife beim `checkStable(HIGH, 5)`-Treffer, vor `stopMove`. Logic-Check: beide Positionen durchlaufen denselben 5-Sample-Filter, der Bias hebt sich bei der Differenz auf. Diagnose-Log `CAL: check d=… exp=… Δ=… (…‰)` läuft jetzt immer.
- **Folge-Befund Hardware-Test 2026-05-05:** rc2-Fix war notwendig aber nicht hinreichend. Selbst mit korrektem p2Pos-Capture lag der Vergleich der gemessenen P1+P2-Breite gegen die aus 3-Touch berechnete `triggerEnd-triggerStart` bei Δ ~45 % → SKIP fiel weiterhin auf 3-Touch zurück. Methodik-Mismatch: 3-Touch fährt entgegengesetzte Drehrichtungen pro Kante (Sensor-Hysterese), P1+P2 dieselbe Drehrichtung — Werte schlicht nicht direkt vergleichbar.
- **Self-Consistency-Fix v4.1.4:** Neues NVS-Feld `fastWidthSteps` (Schema 4002 → 4003). Wird im 3-Touch-Pfad gleich aus der MITgemessenen P1+P2 dieser Run gespeichert. Skip-Vergleich nun gegen `fastWidthSteps` (Apples-vs-Apples). Verifikation: 1. Calib initialisiert (3-Touch + fastW=186 saved), 2. Calib SKIP_OK mit Δ=1 (5‰), Calib-Zeit von ~15 s auf ~5 s.

### [ID 2] ElegantOTA Reboot missing loop()
- **Symptom:** Browser-/curl-Upload an `/ota/upload` meldet `OK`, Board läuft mit alter Firmware weiter.
- **Root Cause:** `ElegantOTA.loop()` fehlte im `loop()` von `main_v4.cpp` bis 4.1.2. Der `_reboot=true`-Flag wurde gesetzt aber nie ausgewertet (würde 2 s nach Upload in `ElegantOTA.loop()` `ESP.restart()` triggern).
- **Fix:** `#include <ElegantOTA.h>` in `main_v4.cpp` + `ElegantOTA.loop();` neben `ArduinoOTA.handle()` in `void loop()`.
- **Workaround beim Upgrade 4.1.2 → 4.1.3:** einmaliger Power-Cycle.

### [ID 3] NoiseEngine Redundancy
- **Befund (Audit 2026-05-04):** `v4/src/L5_programs/synthesis/Synthesis.cpp::tick()` enthält eine vollständige Kopie der Noise-Shaping-Logik, statt die Klasse aus `NoiseEngine.h` zu nutzen.
- **Fix-Strategie:** `NoiseEngine` in `Synthesis` instanziieren und Sampling an `getVal()` delegieren.
- **Status:** backlog, nicht in 4.1.3 (User-Regel: fertige Features einfrieren). Eigener Refactor-Commit/-Tag.

### [ID 4] Tacho ISR Dead Code
- **Befund:** `v4/src/L1_hal/Hal_Tacho.cpp` enthält ISR-Templates und `attachInterrupt`-Versuche, die wegen GPIO-15-Strapping-Pin-Problematik aufgegeben wurden — produktiv läuft 1 kHz `tachoPollTask`.
- **Aktion:** Toten ISR-Code entfernen für Bauhaus-Strenge.
- **Status:** backlog.

### [ID 5] Z-Motor Sweet-Spot vs Hard-Limit
- **Befund:** Charakterisierung Z-Motor (2026-05-02): Sweet-Spot Floor bei 1000 mA, aber `MOTOR_CURRENT_HARD_MAX = 900` mA in `Characterization.cpp` (Pancake-Datenblatt-Soft-Limit).
- **Entscheidung:** 900 mA als Produktiv-Limit beibehalten, dokumentieren dass Performance leicht darüber peakt. Bei Dauerbetrieb >900 mA thermische Last beobachten.

### [ID 6] Tacho-Präzision ms → µs
- **Symptom:** Bei >1000 RPM (Z-Motor) feuerte der Watchdog spurious Stalls — gemessenes `realRpm` schwankte um ±5 %, Schwellwert war 8 %.
- **Root Cause:** `HalTacho` und `MotorProfile` nutzten `millis()`. Bei 3000 RPM ist eine Umdrehung 20 ms — 1 ms Jitter = 5 % Messfehler.
- **Fix v4.0.1/4.0.2:** Migration auf `micros()` + `periodUs`/`lastLowUs`/`NOISE_FILTER_US=5000` in `Hal_Tacho.cpp`, `MotorProfile.cpp/h` und `/watchdog`-JSON-Echo. Schema-Bump `MotorProfileNs::NVS_VER` 4001 → 4002 (alte Profile invalidiert).

### [ID 7] SynthesisTask blockierte Movement
- **Symptom:** Bei Multi-Motor-Engineering-Tests fror die Synthese ein, andere Motoren reagierten nicht.
- **Root Cause:** `SynthesisTask` und `MovementTask` teilten sich Core 1, blockierende Tests in der `MovementTask` ließen die Synthese hängen.
- **Fix v4:** `SynthesisTask` als eigener Task auf Core 1 mit Prio 2, Service-Tasks bleiben Prio 1. Synthese läuft stabil mit 100 Hz, auch während laufender Tests.

### [ID 8] FreqSweep um Mitte → 0 Pulse
- **Symptom:** Bei kleinen Amplituden (hohe Frequenzen) erzeugte der FreqSweep keine Tacho-Pulse — Auswertung unmöglich.
- **Root Cause:** Schwingung erfolgte um die Sensor-Mitte. Bei amp < ½ Zungenbreite verlässt der Motor die Zunge nie → kein Flanken-Wechsel → 0 Pulse.
- **Fix v4:** Schwinge um `triggerStartDeg` (Sensor-Kante) statt um die Mitte. Bei jeder Amplitude (auch < 1 °) entsteht eine Flanken-Kreuzung, solange Motor die Schritte hält. Plus: `EdgeTouch backOff` 0.05 → 0.15 rev, sonst false-positive Sensor-LOW beim Re-Anfahrt.

### [ID 9] Stall zerstörte Home-Position
- **Symptom:** Nach Stall im SpeedTest/CurrentSweep waren Folgetests in falschen Absolut-Positionen.
- **Fix v4:** `resetMotorState(i, wasStalled)` in `Characterization.cpp` ruft bei `wasStalled=true` automatisch `Homing::run(i)` auf. Im Telemetrie-Log sichtbar als „Stall erkannt -> Re-Homing…".

### [ID 10] Calib-Bug 3a: P1/P2 mit stopMove dazwischen
- **Symptom:** Phase 2 (Austritt) feuerte sofort nach Phase 1 (Eintritt) → false-positive HIGH → ungültige Zungenbreite.
- **Root Cause:** Zwischen P1-Trigger und P2-Such-Start lag ein `stopMove()`. Decel-Overshoot (~46 ° bei 3500 sps + 15 k Acc) trug den Motor außerhalb der Zunge, die Such-Schleife sah sofort HIGH.
- **Fix v4:** P1+P2 als durchgehende CW-Bewegung, **kein** stopMove dazwischen. Zungenbreite = `pos(P2) − pos(P1)` mit gleichem Bewegungs-Bias auf beiden Seiten.

### [ID 11] Calib-Bug 3b: EdgeTouch backOff zu klein
- **Symptom:** Beim 3-Touch-Re-Anfahrt landete der Motor noch in der Zunge → checkStable LOW sofort positiv → falsche Kantenposition.
- **Fix v4:** `backOff` 0.05 → 0.15 rev (54 ° bei 16 MS), sicher außerhalb typischer Zungenbreite (~30 °). Plus `maxDelta`-Guard für maximalen Such-Weg.

### [ID 12] Calib-Bug 3c: EdgeTouch Return-Wert mehrdeutig
- **Symptom:** Returnwert 0 wurde als „kein Touch" interpretiert, obwohl die Stepper-Position legitim 0 sein kann.
- **Fix v4:** `LONG_MIN`-Sentinel als „kein Touch", echte Position kann jeden anderen Wert annehmen.

### [ID 13] ArduinoOTA-Instabilität → ElegantOTA als Browser-Pfad
- **Symptom:** ArduinoOTA über espota.py reagierte sporadisch nicht auf UDP-Discovery, vor allem aus WSL2 (NAT-UDP-Rückantwort).
- **Fix v4.0.2:** ElegantOTA als HTTP-Browser-Updater an `/update` (Auth admin/12345678) integriert. ArduinoOTA bleibt für Flashbox-Pfad. Reboot-Bug in 4.1.2 → siehe ID 2.

### [ID 14] Phase 6 GUI: Slider-max hardcoded
- **Symptom:** User konnte mit Slidern Engine-Parameter setzen, die über die in NVS gespeicherten Charakterisierungs-Grenzen hinausgingen.
- **Fix v4.1.0 (Phase 7A):** Neuer `/bounds`-Endpoint exposet `CalibrationData.maxRpm/maxAccel/learnedCurrentMA/sgThrs`. Slider-`max`-Attribute werden beim Page-Load aus `/bounds` gesetzt. `Synthesis::start()` cappt zusätzlich serverseitig (defense in depth) gegen Direktzugriff via `/set`.

### [ID 15] Phase 6 GUI: /set ohne JSON-Echo
- **Symptom:** Nach `/set` wusste das JS nicht, welcher Wert wirklich übernommen wurde (z. B. nach Engine-Cap-Clipping). Slider-Anzeige driftete vom realen State weg.
- **Fix v4.1.1 (Phase 7B):** `/set` antwortet mit JSON-Echo des kompletten `v4::rt`-State. JS aktualisiert Slider+Anzeige aus dem Echo.

### [ID 16] Phase 6 GUI: Canvas ignoriert Engine
- **Symptom:** Canvas zeigte lokale Simplex-Animation mit `off += .4` und ignorierte `v4::rt` komplett. Engine-Parameter-Änderungen waren visuell unsichtbar.
- **Fix v4.1.2 (Phase 7C):** `Synthesis::getPreviewBytes(buf, len)` API liefert je nach `moveType` Engine-Samples (Noise 32×32 Pixmap, Wave 128 Bytes mit Bit7-Phasen-Marker, STEP/Idle = 0). `/preview`-Endpoint serialisiert als JSON. Status-Polling von 500 ms auf 100 ms (10 Hz) hochgezogen für die Motor-Dials.

### [ID 17] Phase 6 GUI: Wellenform-Wechsel ohne Canvas-Reaktion
- **Symptom:** Mode-Switch auf Sinus/Sawtooth/Square (`moveType` 3–5) änderte den Motor-Output, Canvas blieb bei Simplex-Animation.
- **Fix v4.1.3 (Phase 7D):** Canvas-IIFE entfernt, `setInterval(drawPreview, 100)` pollt `/preview`. Drei Pfade: `drawNoise` (32×32 → 128×128 Upscale), `drawWave` (Polyline + orange Phasen-Marker bei Bit7), `drawIdle` (gepulster Hue). `visibilityState`-Guard.

### [ID 18] FreqSweep „Show 7/7" → 7 Impulse, kein Stall
- **Symptom:** Charakterisierungs-Vorführung lieferte nur sieben Tacho-Pulse, der Motor stallte bei keiner Frequenz. Damit kein wissenschaftlich verwertbarer Wert.
- **Root Cause:** Linearer Chirp 10–200 Hz mit fester Amplitude `acc/(16·f²)·0.9`. Bei jeder Frequenz war die Amplitude unterhalb der Stall-Schwelle.
- **Versuchter Fix in 4.1.3-rc1 (Plan B, ZURÜCKGEROLLT):** 10 log-spaced Bänder, Bisektion bis Stall, Learning-Pfad. Auf realer Hardware zwei harte Bugs: ID 21 (Re-Home-Race) + ID 22 (Hysterese-Floor-Detektor). Code in 4.1.3-rc3 wieder ersetzt durch den linearen Chirp aus 4.1.2.
- **Status:** zurück auf bekannten Zustand. Echter Fix bleibt Backlog — nächster Versuch braucht Stall-Detektor, der zwischen „Motor stallt" und „Bewegung zu klein für Sensor-Hysterese" unterscheiden kann (z.B. TMC StallGuard cross-checked mit Tacho-Cross-Counting bei garantiert sensor-überquerender Mindest-Amplitude).

### [ID 21] FreqSweep v2 Re-Home-Race (Hardware-Test 2026-05-05)
- **Symptom (Run #4 LEARN auf Z, 4.1.3-rc1):** Bei f=10 amp=265 nur 1 Puls statt 4 erwartet → STALL klassifiziert → `Homing::run()` → `EdgeTouch MZ: miss` → `HOME: Touch fehlgeschlagen`. Folgetests an unbekannter Position, Bisektion kollabierte.
- **Root Cause:** `fsRehomeToEdge()` ruft `Homing::run()` auch dann, wenn der Motor weit weg von der Sensor-Zunge gestrandet ist. Homing erwartet die Zunge in Reichweite einer 2-rev-CW-Suche, was nach Stall in einem schon ausgelenkten Zustand nicht garantiert ist.
- **Status:** Code zurückgerollt, alter linearer Chirp aktiv. Ein zukünftiger v2 muss das Re-Home-Verfahren robuster machen (z.B. mehrfachen Re-Anlauf mit größerer Suchreichweite) oder Stall-Tests so kurz halten, dass die Position nicht weit driftet.

### [ID 22] FreqSweep v2 Hysterese-Floor-Detektor (Hardware-Test 2026-05-05)
- **Symptom (Z-Motor, mehrere Bänder):** Bisektion lief bei 10 Hz, 36 Hz, 50 Hz, 69 Hz, 96 Hz auf `stallAmp=5` (= `FREQ_AMP_MIN`). Bei kleinen Amplituden lieferte der Tacho 0 Pulse → Detektor `pulses < swings/2` klassifiziert als Stall → Bisektion bringt low immer kleiner.
- **Root Cause:** Die Sensor-Hysterese bedeutet, dass amp < irgendeine Mindest-Strecke (~hysteresis_in_steps) keine Crossings erzeugt, auch wenn der Motor mechanisch sauber schwingt. Mein Detektor unterscheidet das nicht von echtem Stall.
- **Status:** Code zurückgerollt. Korrektur-Idee für späteren v2: vor Tests an einer Frequenz erst `min_detectable_amp` empirisch bestimmen (Bisektion gegen 0 Pulse, ohne Stall-Klassifizierung), dann Stall-Suche nur oberhalb dieser Floor. Oder den Detektor unabhängig vom Tacho machen (TMC StallGuard).
