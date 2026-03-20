# perlinnoise v2

Stand: 2026-03-13

## Ziel

V2-Arbeitsbereich fuer den Umbau der `perlinnoise`-Firmware auf dem FYSETC-E4-Board.
Fokus dieses ersten Stands:

- offizielles E4-Boardmaterial zusammentragen
- den aktuellen Motorcode in `perlinnoise` einordnen
- die Idee bewerten, einen 3-Pin-Endstop als globalen Stop fuer alle 4 Motoren zu nutzen

## V2-Randbedingungen aus der Idee

- V2 hat pro Motor einen Hall-Sensor.
- V2 soll spaeter per ESP-NOW mehrere Boards koppeln koennen.
- ESP-NOW ist als Architekturidee wichtig, fuer das aktuelle Homing-Problem aber noch nicht relevant.
- Annahme fuer diese Notiz: Das Projekt bleibt auf dem FYSETC-E4. Im letzten Input wurde einmal "E3" genannt; hier werte ich das als Versprecher, weil Projekt und Pinlage bisher auf E4 zeigen.

## Ist-Zustand in `perlinnoise`

Quelle: `../src/main.cpp`

- 4 Motoren laufen als `AccelStepper`-Instanzen: `X`, `Y`, `Z`, `E`
- STEP/DIR-Mapping aktuell:
  - `X_STEP=27`, `X_DIR=26`
  - `Y_STEP=33`, `Y_DIR=32`
  - `Z_STEP=14`, `Z_DIR=12`
  - `E_STEP=16`, `E_DIR=17`
- TMC2209-UART aktuell auf `Serial2`:
  - `UART_RX=21`
  - `UART_TX=22`
  - Adressen: `Z=0`, `X=1`, `E=2`, `Y=3`
- Alle vier Treiber teilen sich einen Enable-Pin:
  - `ENABLE_PIN=25`
  - Aktueller Code setzt ihn beim Start auf `LOW`, also aktiv
- Die Firmware nutzt derzeit keine Endstop-Eingaenge.
- "Stop" ist aktuell nur softwareseitig:
  - bei `running=false` wird fuer alle vier `steppers[i]->stop()` aufgerufen
  - ein externer Hardware-Stop ist bisher nicht implementiert

## Offizielle E4-Unterlagen

- FYSETC-E4-Doku: <https://wiki.fysetc.com/docs/E4>
- FYSETC-E4-Boarddateien / Download-Ziel fuer Schaltplan und PCB: <https://github.com/FYSETC/FYSETC-E4>
- ESP32-Datenblatt: <https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf>
- ESP32 GPIO-Hinweise: <https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/gpio.html>

### Relevante Board-Fakten aus der FYSETC-Doku

- Board: FYSETC E4 V1.0
- MCU: ESP32-WROOM-32E / ESP32-WROOM-32UE
- Treiber: 4 x TMC2209
- Endstops: 3
- Die Doku nennt fuer das Board genau diese Endstop-Signale:
  - `X-MIN -> GPIO34`
  - `Y-MIN -> GPIO35`
  - `Z-MIN / Z-PROBE -> GPIO15`
- FYSETC beschreibt zusaetzlich einen dedizierten Z-Probe-Anschluss mit waehlbarer Versorgung fuer 5 V oder Eingangsspannung.

### Relevante ESP32-Fakten fuer die Endstop-Idee

- `GPIO34` und `GPIO35` sind input-only und haben keine softwareseitigen Pull-ups/Pull-downs.
- `GPIO15` ist beim ESP32 `MTDO` und wirkt beim Reset als Strapping-Pin mit Default-Pull-up.
- Daraus folgt:
  - `X_MIN` und `Y_MIN` sind elektrisch unkritische reine Eingangsleitungen, brauchen aber eine saubere externe Pegelfuehrung.
  - `Z_MIN` ist als Eingang nutzbar, muss aber so beschaltet werden, dass das Reset-/Boot-Verhalten des ESP32 nicht unabsichtlich beeinflusst wird.

## Bewertung der V2-Idee

### Was gut passt

- Ein einzelner Endstop als globaler Not-Stop fuer alle 4 Motoren ist mit dem E4 gut machbar.
- Grund: Die bestehende Firmware fuehrt alle 4 Achsen in einer gemeinsamen `loop()` und alle Treiber haengen am gemeinsamen `ENABLE_PIN=25`.
- Damit kann ein einziger Eingang gleichzeitig alle Motoren betreffen.

### Was nicht direkt geht

- Vier unabhaengige Endstops fuer vier Motoren gehen auf dem nackten E4 nicht direkt.
- Grund: 4 Motoren, aber nur 3 Endstop-Eingaenge.

### Praktische Auslegung fuer den Umbau

Wenn die Idee lautet "ein 3-Pin-Endstop fuer Z soll alle 4 Motoren stoppen", ist das technisch plausibel.

Empfohlene Reaktion im Code bei Trigger:

1. Endstop-Zustand einlesen.
2. Globalen Safety-Status setzen, nicht nur `running=false`.
3. Sofort alle Bewegungen stoppen.
4. Optional `ENABLE_PIN` auf `HIGH` setzen, wenn ein echter stromloser Hard-Stop gewuenscht ist.
5. Zustand verriegeln (`latched`), bis bewusst quittiert wird.

### Auswahl des Eingangs

Option A: `Z_MIN / GPIO15`

- passt gut, wenn du bewusst den Z-Eingang fuer den globalen Stop repurposen willst
- hat den Vorteil, dass FYSETC diesen Pfad explizit fuer Z-Endstop / Z-Probe vorsieht
- Nachteil: `GPIO15` ist reset-relevant; die Beschaltung muss auf Boot-Sicherheit geprueft werden

Option B: `X_MIN / GPIO34` oder `Y_MIN / GPIO35`

- aus ESP32-Sicht einfacher, weil reine Input-Pins
- Nachteil: externe Pull-up/Pull-down-Beschaltung ist Pflicht

## Hall-Sensor-Homing mit nur einem Eingang

### Zielkonflikt

- 4 Motoren haben je einen Hall-Sensor.
- Das Board bietet fuer klassische Endstop-Nutzung nur sehr wenige direkt nutzbare Eingange.
- Deshalb ist nicht nur die Firmware, sondern vor allem die elektrische Verschaltung entscheidend.

### Wichtige elektrische Grundregel

Vier Sensorausgaenge darf man nicht einfach blind parallel zusammenschalten.

- Wenn die Hall-Sensoren einen Open-Collector- oder Open-Drain-Ausgang haben, ist ein gemeinsamer Sammelbus machbar.
- Wenn die Hall-Sensoren Push-Pull-Ausgaenge haben, ist direktes Zusammenlegen nicht zulaessig, weil Sensoren gegeneinander treiben koennen.
- In dem Fall braucht es mindestens Dioden-ODER, Transistorstufe oder besser gleich einen kleinen Multiplexer / Expander.

### Stromfrage

Der kritische Punkt ist hier normalerweise nicht "zu wenig Strom".

- Ein ESP32-GPIO als Eingang ist hochohmig.
- Das Problem ist nicht Laststrom am Eingang, sondern:
  - saubere Pegelbildung
  - Ausgangstyp des Hall-Sensors
  - eindeutige Zuordnung, welcher Motor den Trigger erzeugt hat
- Praktisch heisst das:
  - ein gemeinsamer Bus ist elektrisch oft moeglich
  - die Signalauswertung wird dabei der eigentliche Engpass

## Bewertung deiner Homing-Ideen

### Idee 1: alle 4 Sensoren kaskadiert, Motoren nacheinander anfahren

Bewertung: beste Basisloesung.

- Nur ein Motor bewegt sich.
- Wenn der Sammel-Eingang ausloest, ist die Zuordnung eindeutig: der gerade drehende Motor hat seinen Magneten gefunden.
- Das Verfahren ist einfach zu debuggen.
- Es ist robust gegen Timing-Fehler und braucht keine clevere Rekonstruktion.
- Nachteil: am langsamsten.

Fazit:

- Das ist die technisch sauberste erste V2-Version.
- Wenn du schnell zu einer belastbaren Referenzfahrt kommen willst, nimm diese Variante.

### Idee 2: alle 4 drehen 360 Grad, Treffer spaeter ueber Schrittzaehlung rekonstruieren

Bewertung: moeglich, aber unnoetig fragil.

- Du bekommst nur eine gemeinsame Triggerfolge wie z. B. `12, 44, 111, 233`.
- Die Zuordnung Sensor -> Motor ist ohne Zusatzannahmen nicht direkt eindeutig.
- Schrittverluste, unterschiedliche Rampen oder Prellen machen die Rekonstruktion fehleranfaellig.
- Du loest das Problem algorithmisch, das man mechanisch/ablaufseitig viel einfacher loesen kann.

Fazit:

- Als Forschungsansatz interessant.
- Fuer ein robustes Homing nicht meine Empfehlung.

### Idee 3: alle 4 drehen gleichzeitig mit unterschiedlichen Geschwindigkeiten

Bewertung: clever, aber fuer V2 als Start unnoetig kompliziert.

- Die unterschiedliche Geschwindigkeit erzeugt theoretisch eine Signatur pro Motor.
- Praktisch wird die Zuordnung aber empfindlich gegen:
  - Beschleunigungsrampen
  - verlorene Schritte
  - Trigger-Jitter
  - nahezu gleichzeitige Treffer
- Dazu kommt mehr Firmware-Komplexitaet fuer wenig realen Gewinn.

Fazit:

- Machbar, aber eher zweite oder dritte Ausbaustufe, nicht die erste.

### Idee 4: jeder Motor einzeln, findet Referenz, faehrt dann auf 180 Grad Parkposition, naechster Motor folgt

Bewertung: beste konkrete Betriebsstrategie.

- Das ist im Kern Idee 1 plus definierter Freifahr- / Parklogik.
- Dadurch blockiert der bereits referenzierte Magnet den Sammelbus nicht mehr.
- Die Zuordnung bleibt jederzeit eindeutig.
- Die Mechanik bleibt "e-stop-frei", wenn die Parkposition den Sensorbereich sicher verlaesst.

Fazit:

- Von deinen vier Varianten ist das die beste.
- Ich wuerde genau diese Reihenfolge als V2-Homing-Workflow umsetzen.

## Meine Empfehlung

Empfohlene erste Version:

1. Gemeinsamen Hall-Bus auf einen Eingang legen, aber nur wenn die Sensoren dafuer elektrisch geeignet sind.
2. Immer nur einen Motor aktiv drehen.
3. Trigger erkennen.
4. Nullposition dieses Motors speichern.
5. Motor aus dem Triggerbereich auf definierte Parkposition fahren.
6. Naechsten Motor homing fahren.

Warum diese Variante:

- minimale Firmware-Komplexitaet
- klare Sensorzuordnung
- beste Debugbarkeit
- keine Rekonstruktion aus mehrdeutigen Ereignisfolgen
- spaeter leicht auf schnellere Verfahren erweiterbar

## Noch bessere Varianten

Falls du spaeter mehr Hardware zulaesst, sind diese Varianten besser als ein nackter Sammelbus:

### Variante A: 4 Sensoren auf Dioden-ODER plus 4 zusaetzliche Diagnoseleitungen

- Ein gemeinsamer Trigger stoppt sicher.
- Separate Diagnoseleitungen oder Multiplexer liefern die Identitaet.
- Deutlich robuster als reine Zeit-/Schritt-Rekonstruktion.

### Variante B: Analog-Multiplexer

- Z. B. ein 74HC4051 oder 74HC4067.
- Die Firmware waehlt aktiv Sensor 1..4 aus und liest immer nur einen Kanal.
- Elektrisch sauber und eindeutig.
- Fuer Homing oft die beste Low-Cost-Loesung, wenn GPIOs knapp sind.

### Variante C: IO-Expander

- Z. B. per I2C.
- Alle Hall-Sensoren einzeln lesbar.
- Skalierbar und spaeter auch fuer weitere V2-Funktionen nuetzlich.

### Variante D: Sensoren direkt an Treiber-/Interrupt-/PCNT-Logik auslagern

- Nur sinnvoll, wenn die Mechanik spaeter auch waehrend der Bewegung hochaufloesend ausgewertet werden soll.
- Fuer simples Homing zu aufwendig.

## Klare Empfehlung fuer die aktuelle Frage

- Ja, technisch ist ein gemeinsamer 3-Pin-Hall-Bus grundsaetzlich machbar.
- Nein, das wird nicht am "zu geringen Strom" des Eingangs scheitern.
- Das eigentliche Risiko ist eine unzulaessige Parallelschaltung der Sensorausgaenge oder eine mehrdeutige Trigger-Zuordnung.
- Beste Startloesung: Motoren einzeln homing fahren und den jeweils referenzierten Motor danach aus dem Sensorfenster wegparken.

## Was dafuer noch geklaert werden muss

- Exakter Hall-Sensortyp oder Modulbezeichnung
- Ausgangstyp: open-drain/open-collector oder push-pull
- Aktivpegel: active-low oder active-high
- Bleibt der Sensor nur am Magnetfenster aktiv oder ist er als latch ausgelegt?
- Wie weit muss der Motor nach Trigger wegfahren, damit der Sammelbus sicher wieder frei ist?

## Vorschlag fuer V2-Implementierung

### Firmware-Verhalten

- Neuer globaler Eingang, z. B. `GLOBAL_ESTOP_PIN`
- Neuer Safety-Status, z. B.:
  - `estopTriggered`
  - `estopLatched`
- Zwei Stop-Modi:
  - Soft-Stop: `steppers[i]->stop()`
  - Hard-Stop: Treiber ueber `ENABLE_PIN` deaktivieren
- Debounce fuer mechanische Schalter
- Web/API-Status, damit die UI erkennt, ob die Maschine verriegelt ist

### Sinnvolle erste Ausbaustufe

1. Einen Endstop-Eingang als globalen Safety-Eingang definieren.
2. Polling im Hauptloop einbauen.
3. Bei Trigger alle 4 Achsen stoppen.
4. E-Stop latchen.
5. UI/API fuer Reset spaeter nachziehen.

## Offene Punkte vor dem eigentlichen Umbau

- Meinst du mit "3-Pin E-Stop" einen mechanischen Endschalter, ein fertiges Endstop-Modul oder einen Sensor?
- Soll der Trigger nur bremsen oder die Treiber sofort stromlos schalten?
- Soll der Zustand verriegelt bleiben, bis man ihn manuell ueber Web/API quittiert?
- Soll wirklich `Z_MIN` verwendet werden, oder reicht "ein beliebiger 3-Pin-Endstop-Eingang"?

## Workspace-Hinweis

Im aktuellen Workspace habe ich keinen separaten Ordner `motorsteuerung` gefunden.
Die tatsaechliche Motorsteuerung liegt derzeit in `perlinnoise/src/main.cpp`.
