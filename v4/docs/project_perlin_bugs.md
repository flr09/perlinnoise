# 🔴 Project PerlinNoise v4 — Bug Log

Legend: 🔴 bug | 🟡 minor/refactor | ✅ fixed | 🔵 discovery | ⏳ awaiting verification

Quellen: dieser Eintrag · `AGENT_COORDINATION.md` Lessons · `v4/docs/FSD.md` §9.

| ID | Date | Type | Title | Status |
|---|---|---|---|---|
| 1 | 2026-05-04 | 🔴 | Calibration-Skip Logic Error (Decel-Bias) | ✅ v4.1.3-rc2 + v4.1.4 (Self-Consistency) verifiziert |
| 2 | 2026-05-02 | 🔴 | ElegantOTA Reboot missing loop() | ✅ v4.1.3 ⏳ verify |
| 3 | 2026-05-04 | 🟡 | NoiseEngine Redundancy (Synthesis.cpp duplication) | 🟡 backlog |
| 4 | 2026-05-04 | 🟡 | Tacho ISR Dead Code (Polling preferred) | 🟡 backlog |
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
| 18 | 2026-04-29 | 🔴 | FreqSweep „Show 7/7" lieferte nur 7 Impulse, fuhr nicht in Stall — Hyperbel-Beziehung amp×f² nicht modelliert | 🔴 backlog (v2-Versuch in 4.1.3-rc1 zurückgerollt, siehe ID 21+22) |
| 21 | 2026-05-05 | 🔴 | FreqSweep v2: Re-Home-Race nach Stall bei kleiner amp → EdgeTouch miss → Folgetests an Müll-Position | 🔴 reverted in 4.1.3 (linearer Chirp aus 4.1.2 wieder aktiv) |
| 22 | 2026-05-05 | 🔴 | FreqSweep v2 Stall-Detektor: amp < Sensor-Hysterese erzeugt 0 Pulse, fälschlich als Stall klassifiziert → Bisektion läuft in Floor (stallAmp=5 für mehrere Bänder) | 🔴 reverted in 4.1.3 (NVS-Felder bleiben als Reserve für späteren v2-Versuch mit Stallguard-Cross-Check) |

---

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
