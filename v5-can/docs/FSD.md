# FSD — PerlinNoise v5-can (CAN-Bus-Variante)

**Stand:** 2026-08-30 — Erster lauffähiger Prototyp: **"Sinus" v0.1.0** (Tool-Name, `v5-can/src/main.cpp`), 4-Motoren-Funktionstest mit Live-Dashboard.
**Status:** Code existiert und läuft auf Hardware (3 von 4 Motoren antworten — Motor 4 fehlt Bus-Terminierung, siehe §7). Kein Kunden-Web-Interface, kein Boot-Handshake, keine CAN-ID-Enumeration umgesetzt — das ist bisher ein reines Test-/Tuning-Tool.
**Beziehung zu anderen Zweigen:** Eigenständiger, neuer Aufbau. Unabhängig von `v4` (FYSETC E4, Produktivlinie) und von `v5` (MT6701-Encoder-Redesign, ebenfalls auf E4-Basis). Keiner der bestehenden Zweige wird verändert.

### 1a. Architektur-Wechsel: Punkt-zu-Punkt statt kontinuierlicher Kurve (2026-08-30)

Der ursprüngliche Plan (§3.2, kontinuierliche `0xA4`-Trajektorie alle 150ms neu berechnet — erst Perlin, dann Sinus, dann Dreieck, dann Parabel-Trapez) wurde **verworfen**: das ständige Nachschieben neuer Zwischenziele hat die interne Rampe des Motors laufend unterbrochen ("Gehacke/Gestotter", Chris 2026-08-30).

**Aktuelle Architektur:** Punkt-zu-Punkt-Pendelbewegung. Pro Motor wird **ein** Ziel (±Distance) gesendet, der Motor fährt seine **eigene** interne Rampe (Beschleunigung/Plateau/Bremsen) bis zum Ziel durch. Erst wenn Ankunft erkannt ist (Ist-Winkel nah genug am Ziel + Geschwindigkeit niedrig genug, mit Timeout-Fallback), wird das gegenüberliegende Ziel gesendet. Als Tool-Name **"Sinus"** deklariert (`TOOL_NAME`/`TOOL_VERSION` in `main.cpp`) — die Pendelbewegung zwischen zwei Extremen ist die einfachste Form eines periodischen Bewegungsmusters aus der in §3.6 beschriebenen Modul-Idee, auch wenn die Ziel-Kurve selbst ein Sprung (kein glatter Sinus) ist; die Glättung kommt ausschließlich aus der motoreigenen Rampe.

**Live-tunbare Parameter** (Dashboard-Regler, siehe §3.7): Rampe (`0x34`, dps/s), Speed (`0xA4`-maxSpeed, dps), Distance (Amplitude, °). Änderungen gehen per Klartext-Serial-Kommando (`SET key=value`) von der Flashbox direkt an die Firmware, kein Neuflashen nötig.

**[ANGELEGT, NICHT AKTIV]** Duration (Länge einer Bewegung) und Versatz (Zeitversatz zwischen Motor-Bewegungsstarts) sind als Regler/Variablen vorhanden. Versatz ist voll verdrahtet (wirkt durchgehend, nicht nur beim Booten). Duration ist bewusst **nicht** an die Bewegung angeschlossen — sie würde Distance/Speed/Rampe wechselseitig beeinflussen (analog zur v4-Hyperbel-Kopplung, `v4/docs/FSD.md` §0), die genaue Kopplungsformel muss erst gemeinsam festgelegt werden. Ein Stub (`computeSpeedFromDuration()`) existiert, wird aber nirgends aufgerufen.

---

## 1. Ziel und Scope

Ein neues, eigenständiges Kunden-Gerät: 4 CAN-Bus-Servomotoren, angesteuert von einem LilyGO T-CAN485 (ESP32), erzeugen ein Perlin-Noise-Bewegungsmuster (Fortführung der v1-Idee, wie bei v4/v5). Zielgruppe ist **die Kundin am Gerät**, nicht ein Techniker — das Web-Interface bleibt bewusst technikfrei (keine Rohparameter, keine Diagnose-/Testansichten, siehe globale Qualitätsregel „keine Systemwarnungen auf Endnutzer-Displays").

**Out of scope:** Mechanische Auslegung/Last der 4 Motoren — von Chris bereits bei 15 V praktisch verifiziert, wird hier nicht weiter geprüft.

---

## 2. Hardware-Architektur

### 2.1 Steuerboard: LilyGO T-CAN485 (ESP32)

Reines Kommunikations-Board — kein eigener Stepper-/Servo-Treiberausgang. Läuft aktuell per USB an der `flashbox` (Raspberry Pi 400, siehe `reference_flashbox`-Memory).

Pin-Mapping (verifiziert aus offiziellem Repo `Xinyuan-LilyGO/T-CAN485`, `lib/Mylibrary/pin_config.h`):

| Funktion | GPIO |
|---|---|
| CAN_TX | 27 |
| CAN_RX | 26 |
| CAN_SPEED_MODE | 23 |
| RS485_TX | 22 |
| RS485_RX | 21 |
| RS485_EN | 19 |
| RS485_CALLBACK | 17 |
| ME2107_EN (Boost, versorgt RS485+CAN-Transceiver) | 16 |
| WS2812B_DATA | 4 |

CAN-Transceiver: SN65HVD231 (reiner Bus-Transceiver — der CAN-Controller/TWAI-Peripherie sitzt bereits im ESP32-Silizium, kein externer MCP2515 nötig).

Datenblätter liegen in `datasheets/`: `T-CAN485_schematic.pdf`, `SN65HVD231_CAN_transceiver.pdf`, `MAX13487E_RS485_transceiver.pdf`.

### 2.2 Motoren: 4× LKMTECH MS3506v2 (RMD-S-3506)

Offizielles Datenblatt gesichert (`datasheets/LKMTECH_MS3506_official_specsheet.webp`) und CAN-Protokoll (`datasheets/LKTech_MS3506_CAN_Protocol_V4.3.pdf`, protokollkompatibel zu MyActuator „Servo Motor Control Protocol V4.3", Applicable driver V3).

| Parameter | Wert |
|---|---|
| Rated Voltage | 12 V (Betrieb bei 15 V von Chris verifiziert, liegt im Eingangsbereich 6–16 V) |
| Rated Current | 0,79 A |
| Max Current (abgeleitet, Byte 255 im Protokoll) | ≈ 2,0 A |
| Rated / Max Torque | 0,05 Nm / 0,13 Nm |
| Max Speed | 2100 rpm |
| Encoder | 15-bit magnetisch |
| Getriebeuntersetzung | **nicht dokumentiert** — offen |

**CAN-Bus-Parameter:** 1 Mbps, Standard-Frame, DLC 8.
- Einzelmotor-Kommando: `0x140 + ID` (ID 1–32)
- Broadcast an alle Motoren gleichzeitig: `0x280`
- Antwort: `0x240 + ID`

→ Alle 4 Motoren können an einem physischen Bus hängen, mit individuellen IDs (z. B. 1–4) einzeln oder per Broadcast synchron angesprochen werden.

**Relevante Bewegungskommandos (V2.35, verifiziert 2026-08-28/29 an echter Hardware):**

| Code | Zweck |
|---|---|
| `0x88` | Motor on (**Pflicht vor jeder Bewegung**, sonst reagiert der Motor auf 0x9A, aber nicht auf Motion-Kommandos) |
| `0x80`/`0x81` | Motor off / Motor stop |
| `0xA2` Speed Closed-Loop | Ziel-Drehzahl (0.01dps/LSB) |
| **`0xA4` Multi loop angle control 2** | **Ziel-Winkel multi-turn (0.01°/LSB) + Speed-Limit — unser gewähltes Bewegungskommando** |
| `0xA6` Single loop angle control 2 | Ziel-Winkel 0–360° + Speed-Limit + Richtung |
| `0x92` | Ist-Winkel lesen (motorAngle int64/int32, 0.01°/LSB, multi-turn kumulativ, nie automatisch zurückgesetzt) |
| `0x33`/`0x34` | Beschleunigung lesen/schreiben (RAM, int32, 1 dps/s) |
| **`0x19`** | Aktuelle Position als Motor-Zero ins ROM schreiben („Set Zero") — **[KORRIGIERT, 2026-08-29]:** nicht `0x64`. Laut V2.35 „valid only after reset power" — braucht einen **echten Stromzyklus**, keinen CAN-Reset-Befehl (den gibt's in V2.35 nicht). Reale Konsequenz für die Kunden-Set-Zero-Funktion (§3.4): wirkt evtl. erst nach dem nächsten Boot, nicht sofort — noch zu verifizieren. |
| `0x9B` | Motor-Fehlerstatus zurücksetzen („Clear Error") — bisher nicht eingeplant, aber relevant für den Boot-Handshake (§3.5a). |
| `0x30`/`0x31`/`0x32` | PID lesen/RAM-schreiben/ROM-schreiben (Winkel/Speed/Current je Kp+Ki, direkte Byte-Felder) — für Admin-Interface (§3.5). |
| `0x9A` | Status 1 + Error-Flag (Temp, Spannung, 1-Byte-Fehlerstatus) |

**[VERWORFEN, 2026-08-29]:** `0xA9`/`0x62`/`0x63`/`0x64`/`0x76`/`0x79`/`0x300` aus früheren FSD-Ständen — die stammten aus einem falschen Protokoll-Dokument (V4.3, nicht zu dieser Hardware passend) und existieren im echten V2.35-Protokoll gar nicht. Siehe [[project_perlin_v5_can]]-Memory für die volle Korrektur-Historie.

**Community-Bibliothek `lkm_m5`** (github.com/project-sternbergia/lkm_m5, MIT, ESP32/Arduino, nutzt `ESP32-TWAI-CAN`) bestätigt unabhängig alle obigen Kommandos 1:1 (inkl. `MS_SERIES_MAX_POWER 850`, passend zu unserer Hardware) — **vierte unabhängige Quelle ohne CAN-ID-Set-Befehl**, siehe §3.4a. Noch nicht als Dependency eingebunden (offene Entscheidung: volle Einbindung vs. nur als Referenz), aber deckt zusätzliche, von uns noch nicht implementierte Kommandos auf: `0x80`/`0x81` (Motor off/stop), `0xA0`/`0xA1` (Open-/Torque-Loop), `0xA5`–`0xA8` (Single-/Increment-Angle), `0x90`/`0x91` (Encoder roh lesen/ROM-Offset schreiben), `0x94` (Single-Turn-Winkel), `0x95` (Angle-Loop löschen), `0x9C`/`0x9D` (Status 2/3, Phasenströme).

### 2.3 Stromgrenze (von Chris festgelegt, 2026-08-27)

**[KORRIGIERT, 2026-08-29]:** `0xA4` hat **kein** Strombegrenzungs-Feld pro Kommando (anders als ursprünglich mit `0xA9` angenommen). Die Grenze sitzt ausschließlich in der Treiber-Einstellung **„Max Power"** (MS-Serie) — GUI-Screenshot vom 2026-08-28 zeigt aktuell **850** (von 850 max, also praktisch unlimitiert) und **Max Speed 9000**, **Max Angle 0,00** (vermutlich „kein Limit", nicht „Bewegung blockiert" — durch Live-Test mit sichtbarer 360°-Drehung widerlegt, dass es blockiert). Kein dokumentierter CAN-Befehl gefunden, um „Max Power" zu setzen — nur über LK Motor Tool (USB/RS485) einstellbar.

**Offen:** Die ursprünglich gewünschten „85 % vom Maximalstrom" lassen sich damit aktuell **nicht** als Kommando-Feld umsetzen. Praktikable Alternative: „Max Power" im LK Motor Tool einmalig auf ~85 % senken (von 850 auf ≈722), dann gilt die Grenze automatisch für alle Bewegungsarten. Noch nicht umgesetzt.

---

## 3. Software-Architektur

### 3.1 ESP32 Core-Split

- **Core 0:** WiFi + WebServer (Kunden-Interface) — Standard-ESP32-Zuordnung, WiFi-Stack läuft ohnehin hier.
- **Core 1:** CAN-TX-Loop (TWAI-Treiber) + Perlin-Pattern-Berechnung / Motion-Dispatch — analog zum bestehenden `startMovementTask`-Pattern aus v4 (`Platform.h`).

### 3.2 Architektur-Entscheidung: Positions-Modus (Chris, 2026-08-27)

**Entscheidung: Ziel-Winkel-Modus**, nicht Ziel-Drehzahl. Begründung: Beschleunigung/Geschwindigkeit lassen sich aus der Ziel-Winkel-Trajektorie berechnen (näher am bewährten v4-Ansatz mit `FastAccelStepper`-Zielpositionen).

Konkretes Kommando: **`0xA4`** (Multi loop angle control 2), siehe §2.2. Live auf Hardware verifiziert (2026-08-28/29): sichtbare, saubere Rotation, Ist-Winkel (`0x92`) deckt sich mit dem 30s-Kalibrierlauf (+360°).

**Erwarteter Aufwand, von Chris benannt:** Die PID-Parameter werden vermutlich Tuning pro Motor brauchen — als "könnte noch spannend werden" markiert. **[KORRIGIERT, 2026-08-29]:** V2.35 kennt keine Funktionsindizes wie ursprünglich angenommen — `0x30`/`0x31`/`0x32` (Read/Write RAM/Write ROM) übertragen alle drei Regelkreise **direkt als feste Bytes** in einem Frame: `DATA[2]=anglePidKp, DATA[3]=anglePidKi, DATA[4]=speedPidKp, DATA[5]=speedPidKi, DATA[6]=iqPidKp(Current), DATA[7]=iqPidKi(Current)`. Siehe §3.5 (Admin-Interface) — reale physische Konsequenz beim Tuning: Objekte liegen auf dem Tellerchen jedes Motors, unsauberes PID-Tuning kann zu Überschlag/Herunterfallen führen, nicht nur zu "unschönen" Bewegungen.

### 3.3 CAN-Treiber

ESP32 TWAI-Peripherie (ESP-IDF `driver/twai.h` bzw. Arduino-Wrapper), GPIO 26/27 wie in §2.1.

### 3.4 Web-Interface (Kunde)

**Wiederverwendung aus v4 (verifiziert, 2026-08-29):** v4 hat ein bereits ausgereiftes Player-Interface (`v4/src/L7_web/WebServer.cpp`, `INDEX_HTML`). Direkt übernehmbar für v5-can:
- **`/preview`-Canvas-Mechanismus** (10-Hz-Polling, JSON→32×32-Graustufen-Bild) — Server- und Client-Code fast 1:1 übertragbar, nur Datenquelle (Perlin-Synthesis statt CAN-Winkel) austauschen.
- **Winkel-Dial-SVG + Zero-Button-Pattern** (`buildDials`/`updateDials`) — bildet unsere "Winkelanzeige je Motor" + "Set Zero" direkt ab.
- **Bauhaus-CSS-Designsystem** (`:root`-Variablen, `.btn`, `.dial`, `.strip`) — bereits im "technikfrei/schlicht"-Register, kein externes Framework.
- **Drag-sicheres Slider-Pattern** (`oninput`=lokales Echo, `onchange`=Netzwerk-Send) + Offline-Graying (`body.is-offline{filter:grayscale(.5)}`) aus `PerlinControl_v4.html`.
- **`/api/version`-Schema-Handshake** — verhindert stillen Drift zwischen Firmware und HTML.

**Bewusst NICHT übernommen:** `/bounds`-Hyperbel-Kopplung (TMC2209/Stepper-spezifische Physik), `/test`-Engineering-Seite, Presets-Grid, Recorder/CSV, Timeline-Editor, Motor-Typ/Sensor-Debug-Aktionen — alles laut v4-FSD selbst als "Engineering-Schwere" markiert, nicht kundentauglich.

Rein technikfreies Web-UI, eigener Netzwerkzugang übers Mobilgerät:

- **Start** / **Stop**
- **Geschwindigkeit** (ein Regler, keine Rohparameter)
- **Vorschaubild der Perlin-Wolke** — Wiederverwendung des bestehenden `/preview`-Musters aus v4 (`WebServer.cpp`: 10-Hz-Polling → Canvas-Visualisierung)
- **Winkelanzeige** je Motor (aus der Kommando-Antwort, Feld „Motor angle")
- **„Set Zero"**-Routine je Motor einzeln (Kommando `0x19` — braucht laut Doku „reset power", siehe §2.2-Korrektur; evtl. wirkt der neue Zero-Punkt erst nach dem nächsten Boot, nicht live im UI — noch zu verifizieren)

**[IDEE, offen, Chris 2026-08-29]:** Einer der Motoren könnte per Hand gedreht werden und sein Encoder-Wert (`0x92`) als physischer Dreh-Eingang für die App genutzt werden, z. B. zum Einstellen einzelner Parameter (welche, muss später ausgetestet werden). Noch keine Festlegung, welche Parameter dafür in Frage kommen.

### 3.5a Boot-Handshake (bei jedem Systemstart, vor Freigabe der Kunden-Oberfläche)

Idee von Chris (2026-08-27): Vor jedem Öffnen der Perlin-Oberfläche werden die hinterlegten Werte aus den Motoren ausgelesen und geprüft — nicht nur einmalig bei Installation, sondern bei **jedem** Boot (Auto-Recovery nach Stromausfall, bestehende Projektregel).

Ablauf pro Motor (alle 4 sequenziell oder parallel über den Bus):

1. **`0x9A` lesen** (Status 1 + Error-Flag, 1-Byte-Feld). Bit 0 = Unterspannung, Bit 3 = Übertemperatur (V2.35, siehe §2.2-Korrektur — die 16-Bit-Fehlertabelle aus früheren FSD-Ständen war falsch).
   - Fehler aktiv → **`0x9B`** (Clear Error) versuchen; laut Doku klappt das nur, wenn der Motor tatsächlich wieder im Normalzustand ist.
2. **Zero-Offset abgleichen:** `0x90` lesen (liefert encoder/encoderRaw/encoderOffset), gegen den auf dem ESP32 (NVS) hinterlegten Referenzwert vergleichen.
   - Referenzwert vorhanden und deckt sich → weiter.
   - Referenzwert fehlt (Erstinbetriebnahme) oder weicht ab → Zero setzen: entweder `0x91` (Encoder-Offset direkt ins ROM schreiben) oder `0x19` (aktuelle Position als Zero) — **[OFFEN]** `0x19` braucht laut Doku „reset power", ob das den Boot-Handshake blockiert oder erst beim nächsten Boot wirkt, ist ungeklärt. Neuen Wert in NVS als Referenz sichern.
3. Alle 4 Motoren fehlerfrei (Schritt 1) und Zero bestätigt/gesetzt (Schritt 2) → **„System OK"** auf dem Kunden-Interface, Perlin-Oberfläche wird freigeschaltet.
4. Bleibt ein Motor fehlerhaft (auch nach `0x9B`-Versuch) → Kunden-UI bleibt technikfrei: **kein** Rohfehlercode für die Kundschaft, nur ein generischer nicht-technischer Zustand (z. B. „wird vorbereitet"). Die Fehlerdetails sind ausschließlich über den Dev-/Diagnose-Pfad (§3.5, fixed-IP) sichtbar.

### 3.4a CAN-ID-Vergabe (Enumeration)

Motoren kommen **ohne vorkonfigurierte ID** (Werkszustand: ID 1, `0x141`/`0x241`).

**[KORRIGIERT, 2026-08-29]:** Der ursprüngliche Plan (`0x79`-Kommando an Sonderadresse `0x300`) stammte aus dem falschen V4.3-Dokument und ist für diese Hardware **nicht verifiziert**. Die echte V2.35-Doku hat im kompletten Befehlskatalog (27 Einzelmotor- + 1 Multi-Motor-Kommando) **keinen dokumentierten „CAN-ID setzen"-Befehl**. Im LK Motor Tool (Setting-Tab) existiert ein „Driver ID"-Feld, das vermutlich nur über USB/RS485 mit einem undokumentierten Kommando beschrieben wird. **Offen:** ob `0x79` trotzdem funktioniert (ungetestet), oder ob die ID zwingend einmalig per USB+GUI pro Motor vorkonfiguriert werden muss, bevor die Motoren ans CAN-Bus-Feld gehen.

**Verfeinerte Zuordnungs-Idee (Chris, 2026-08-29):** IDs werden im Zahlenraum **über 20** vergeben (21, 22, 23, 24 — nicht mehr 10–13). Auslöser für die Zuordnung ist nicht mehr nur "ein Motor antwortet auf ID 1", sondern eine **bewusste Umdrehung über 360°**:

1. Der Motor, der als erstes eine Winkeländerung über 360° vollzieht (z. B. von Hand gedreht oder testweise angesteuert), wird als Motor 1 erkannt → **ID 21**.
2. Danach Rückfrage „neuen Motor hinzufügen?" — der nächste Motor, der über 360° bewegt wird, wird Motor 2 → **ID 22** (danach 23, 24 nach demselben Muster).
3. Vorteil gegenüber der reinen Präsenz-Erkennung: eindeutiger, vom Installateur kontrollierter Zeitpunkt der Zuordnung statt reiner Zufalls-Reihenfolge beim Anschließen.

**Wichtige Einschränkung (weiterhin gültig, unabhängig vom Schreibmechanismus):** Solange mehrere Motoren gleichzeitig auf derselben Werks-ID (1) lauschen, kollidieren ihre Antworten auf jede Abfrage dieser ID. Die Vergabe — welcher Mechanismus auch am Ende funktioniert — braucht entweder Motoren einzeln am Bus, oder einen ID-Schreibweg, der nicht über eine gemeinsam gelauschte Adresse läuft.

**Nebenbefund:** Der dokumentierte Multi-Motor-Broadcast (`0x280`, Torque-Control für bis zu 4 Motoren gleichzeitig) verlangt laut V2.35 zwingend IDs **#1–#4** ohne Wiederholung — falls wir diesen speziellen Befehl je nutzen wollen, kollidiert das mit dem "über 20"-Schema. Für unsere Einzelmotor-`0xA4`-Ansteuerung (kein Broadcast) ist das aber irrelevant.

Die „neuen Motor hinzufügen?"-Rückfrage ist ein Inbetriebnahme-/Wartungsvorgang (z. B. Motor-Tausch) — gehört auf den Dev-/Diagnose-Pfad (§3.5, fixed-IP), nicht ins Kunden-UI.

### 3.5 Admin-/Diagnose-Interface (nicht Teil des Kunden-UI)

CAN-Rohausgabe/Diagnose für die Entwicklung, aber:
- nur erreichbar über eine **feste IP-Whitelist** (fixed IP, nicht Teil des Kunden-Netzugangs)
- **klar abschaltbar** (Flag/Build-Option), damit im Produktivbetrieb nichts davon sichtbar ist — deckt sich mit der Projektregel „Monitoring gehört zum Admin, nicht zum Endnutzer".

**PID-Tuning-Panel (Chris, 2026-08-29):** Struktur orientiert sich an v4s Engineering-Seite (`/test` in `WebServer.cpp`, Rohparameter-Zugriff statt kundenfreundlicher Aufbereitung) — inhaltlich aber neu, da v4s TMC2209-Stepper open-loop ohne PID läuft. Für v5-can gebraucht: Kp/Ki je Motor für drei Regelkreise (Winkel, Speed, Current/Torque), per `0x30` lesen, `0x31`(RAM)/`0x32`(ROM) schreiben — direkte Byte-Felder, siehe §3.2-Korrektur.

**Warum das wichtig ist:** Auf dem Tellerchen jedes Motors liegen Gegenstände. Schlecht getuntes PID (zu aggressiv → Überschwingen/Überschlag; zu träge → schlappes Tracking) kann Objekte herunterwerfen oder verrutschen lassen — das ist keine kosmetische Frage, sondern eine physische. Chris' Auftrag: Die konkreten Kp/Ki-Werte sollen **von mir empirisch am echten Motor ausgetestet** werden (schrittweise erhöhen, auf Überschwingen/Nachschwingen bei Sprungantworten prüfen), sobald Hardware wieder verfügbar ist — nicht blind aus Datenblatt-Defaults übernehmen.

### 3.6 Bewegungsmuster-Modul (Sinus, aus v2 übernommen — Chris, 2026-08-29)

`v2/src/main_v2.cpp` (älterer Projektstand, eigenständiges `waveformValue(mType, phase, zShape, edgeC)`-Modul, Zeile ~624) implementiert bereits mehrere parametrische Bewegungsformen — Sinus, Sägezahn, Rechteck — plus Simplex-Noise, alle über eine gemeinsame Phasen-/Form-/Kantenschärfe-Abstraktion:

```js
function waveformValue(mType, phase, zShape, edgeC) {
  if (mType === 3) { val = Math.sin(phase); }              // Sinus
  else if (mType === 4) { /* Saegezahn, zShape=Skew */ }
  else if (mType === 5) { /* Rechteck, zShape=Duty-Cycle */ }
  // tanh-Soft-Clipping ueber edgeC: 0=hart, 2=seidenweich
  const softness = Math.max(0.0, 2.0 - edgeC);
  if (softness > 0.05) { const k = 3.0/softness; val = Math.tanh(val*k)/Math.tanh(k); }
  return val;
}
```

**Warum Sinus für v5-can besonders geeignet ist:** Anders als Perlin-/Simplex-Noise (organisch, aber lokal unvorhersehbare Ableitung) ist eine reine Sinuswelle überall glatt (stetige Beschleunigung, kein Ruck) — passt gut zur Tellerchen-Anforderung aus §3.5 (kein Überschlag). Sinus ist daher ein guter Kandidat als **Standard-/Sicherheits-Bewegungsmuster**, Perlin-Noise als "organischere" Option daneben.

**Für v5-can übernehmen:** Die Phasen-/Form-Abstraktion (nicht die Noise-Engine selbst, die bleibt v4-spezifisch) als Vorbild für den Pattern-Layer in Phase 6 (Perlin-Pattern-Port) — Kunden-UI bekäme dann perspektivisch eine Muster-Auswahl (mind. Sinus, optional Perlin) statt nur einer festen Perlin-Trajektorie. Noch nicht in den Funktionalen Anforderungen (§5) verankert — Ideensammlung, keine Entscheidung.

**[UMGESETZT, 2026-08-30]:** Siehe §1a — die tatsächliche Implementierung ist eine Punkt-zu-Punkt-Pendelbewegung ("Sinus" als Tool-Name), nicht die kontinuierliche `waveformValue()`-Kurve. Glättung kommt aus der motoreigenen Rampe, nicht aus extern berechneten Zwischenpunkten.

### 3.7 Live-Dashboard (Entwicklungs-Tool, nicht Kunden-UI)

`v5-can/dashboard.py`, läuft auf der Flashbox (Port 8090), liest die serielle Verbindung zum T-CAN485 mit:
- Pro Motor: Soll (Ziel), Ist (aktueller Winkel), Speed, Power, Zeit seit letztem Update.
- Graph pro Motor: Speed (blau), Ziel (orange), Ist (grün) über Zeit, feste Winkel-Skala (±400°, Marker bei 180°/360°).
- Regler: Rampe, Speed, Distance (voll live-wirksam), Versatz (voll live-wirksam, durchgehend — nicht nur beim Booten), Duration (angelegt, bewusst nicht verdrahtet, siehe §1a).
- Bidirektional: Dashboard schreibt `SET key=value`-Kommandos über dieselbe serielle Verbindung zurück an die Firmware.

Kein Ersatz für das geplante Kunden-Web-Interface (§3.4) oder Admin-Interface (§3.5) — reines Tuning-Werkzeug für die aktuelle Entwicklungsphase.

---

## 4. Implementierungsphasen

| Phase | Inhalt | Deliverable / Test |
|---|---|---|
| **1. Bring-up Einzelmotor** ✅ | 1× MS3506 an T-CAN485, TWAI-Init, `0x88`+`0xA4`-Kommando (Ziel-Winkel) senden, Antwort lesen. | **Erledigt 2026-08-28/29:** Motor fährt Zielwinkel sichtbar, Telemetrie (Temp/Speed/Encoder, `0x92`-Ist-Winkel) kommt zurück und deckt sich mit Beobachtung. |
| **2. CAN-ID-Enumeration** | Schreibmechanismus offen (§3.4a — `0x79` unverifiziert, evtl. USB+GUI nötig). Zuordnung per bewusster >360°-Drehung, IDs 21/22/23/24. | Alle 4 Motoren tragen eindeutige IDs, keine Kollision, Reboot-fest. |
| **3. 4-Motor-Bus** | Alle 4 Motoren (IDs 21–24) an einem Bus, Broadcast-Test (`0x280`, sofern IDs dafür passend — sonst nur Einzelansteuerung). | Alle 4 einzeln ansprechbar. |
| **4. Strombegrenzung** | „Max Power" im LK Motor Tool auf ~85 % senken (§2.3 — kein CAN-Kommando dafür gefunden). | Motorstrom bei Volllast ≈ 85 % des Maximalstroms, nachgemessen. |
| **5. Boot-Handshake** | `0x9A`-Fehlerabfrage + Zero-Abgleich pro Motor, §3.5a. | Nach Kaltstart ohne manuellen Eingriff „System OK" oder sauberer nicht-technischer Fehlerzustand. |
| **6. Perlin-Pattern-Port + PID-Tuning** | Noise-Generator-Logik (Konzept aus v4 `Synthesis`) auf 4 CAN-Motor-Ziele umlegen; Core-Split Web/Motion; PID (`0x30`/`0x31`/`0x32`, §3.5) je Motor empirisch einstellen — kein Überschwingen, damit Objekte auf dem Tellerchen nicht herunterfallen. | 4 Motoren folgen synchroner Trajektorie sauber ohne Schwingen/Nachlauf. |
| **7. Kunden-Web-Interface** | Start/Stop/Speed/Preview/Winkelanzeige/Set-Zero, technikfrei, hinter Boot-Handshake „System OK" freigeschaltet — Basis: v4s `/preview`+Dial-Pattern, siehe §3.4. | Bedienbar ohne technisches Wissen, keine Diagnosewerte sichtbar. |
| **8. Admin-/Diagnose-Interface** | CAN-Rohausgabe + PID-Tuning-Panel (§3.5) hinter Fixed-IP-Gate, per Flag abschaltbar; „neuen Motor hinzufügen?"-Rückfrage aus §3.4a lebt hier. | Nur von whitelisteter IP erreichbar; im Produktiv-Build deaktivierbar. |

Jede Phase wird hardware-verifiziert, bevor getaggt wird (bestehende Projektregel: Test vor Tag).

---

## 5. Funktionale Anforderungen

**Must:**
- M1: 4 Motoren individuell (`0x140+ID`) und per Broadcast (`0x280`) über CAN ansteuerbar.
- M2: Web-Interface ausschließlich für Kunden: Start, Stop, Geschwindigkeit, Vorschaubild, Winkelanzeige je Motor, Set-Zero je Motor — keine technischen Rohparameter, keine Diagnose.
- M3: Strombegrenzung 85 % vom Maximalstrom (≈1,7 A, DATA[1]≈215) aktiv über `0xA9` (§2.3, §3.2).
- M4: Core-Split Web (Core 0) / CAN-Motion (Core 1).
- M5: Boot-Handshake bei jedem Systemstart (§3.5a) — Fehlerabfrage + Zero-Abgleich pro Motor, „System OK" schaltet die Kunden-Oberfläche erst frei. Kein manueller Eingriff nötig (Auto-Recovery-Regel).

**Should:**
- S1: Dev-CAN-Diagnoseansicht hinter Fixed-IP-Whitelist, per Flag abschaltbar.
- S2: Set-Zero schreibt persistent (ROM, Kommando `0x19`), übersteht Reboot — Wirkzeitpunkt (sofort vs. erst nach Power-Cycle) noch zu verifizieren.
- S3: Telemetrie (Temperatur/Strom je Motor) serverseitig geloggt, nicht auf dem Kunden-UI sichtbar.

**Nice-to-have:**
- N1: CAN-Bus-Fehlererkennung (Bus-Off, Motor-Timeout) mit sauberer Degradierung statt Absturz — Playback darf nie stoppen (bestehende Projektregel).
- N2: OTA-Update-Pfad wie v4 — `ElegantOTA.loop()`-Pflicht beachten (bekannter Stolperstein aus v4).

---

## 6. Verworfene Ansätze (dokumentiert)

- **T-CAN485 als Ersatz für FYSETC E4:** verworfen — das Board hat keine eigenen Stepper-/Servo-Treiberausgänge, nur CAN/RS485-Kommunikation. Stattdessen eigenständiger neuer Aufbau mit CAN-nativen Motoren.

---

## 7. Offene Fragen & Annahmen (explizit)

- **[GEKLÄRT, 2026-08-29]** Positions-Modus: `0xA4` (nicht `0xA9` — existiert nicht), live auf Hardware verifiziert (§3.2). Strombegrenzung läuft NICHT über ein Kommando-Byte, sondern nur über „Max Power" im Treiber (§2.3, offen wie per CAN setzbar).
- **[OFFEN, 2026-08-29]** CAN-IDs der 4 Motoren — `0x79` empirisch getestet (inkl. Reset-Versuch), **keine Reaktion**. Vier unabhängige Quellen (offizielle V2.35-Doku, offizielles LK-Firmware-Demo, Community-Lib `lkm_m5`, offizielles GUI-Handbuch) zeigen übereinstimmend **keinen** CAN-Befehl zum ID-Setzen. Einziger noch offener Pfad: „Save Setting" + „Reboot Device" in der LK Motor Tool GUI (bündelt vermutlich den ganzen Parameterblock inkl. Driver-ID), noch nicht getestet. IDs sollen bei Erfolg im Bereich 21+ liegen, Zuordnung per bewusster >360°-Drehung (§3.4a).
- **[OFFEN]** Bus-Terminierung: CAN braucht 120 Ω Abschlusswiderstände an beiden Busenden — Verkabelung/Topologie noch nicht festgelegt.
- **[OFFEN]** Gemeinsame Spannungsversorgung für 4× Motor (6–16 V, real bei 15 V betrieben) + T-CAN485 (5–12 V Boost-Eingang).
- **[ANNAHME]** Umrechnung 85 % Maximalstrom → Protokoll-Byte 215 (§2.3) — vor Produktivbetrieb an echter Hardware nachmessen.
- **[RISIKO]** Getriebeuntersetzung der MS3506v2 nicht dokumentiert — Winkelauflösung am Abtrieb unbekannt, beeinflusst Positions-Genauigkeit der Perlin-Trajektorie.
- **[ANNAHME]** Mechanische Last/Eignung der Motoren — laut Chris bereits bei 15 V praktisch verifiziert, hier nicht weiter geprüft (§1, Out of scope).
- **[GEKLÄRT, 2026-08-27]** Erstinbetriebnahme des Boot-Handshakes (§3.5a): Mechanik dreht frei (kein Anschlag, keine definierte Ausgangslage nötig) — automatisches Zero-Setzen bei fehlendem NVS-Referenzwert ist unbedenklich, kein expliziter Einricht-Schritt nötig.
- **[OFFEN]** Wie soll der generische Kunden-Fehlerzustand (§3.5a, Schritt 4) konkret aussehen, wenn ein Motor dauerhaft fehlerhaft bleibt — nur "wird vorbereitet" ohne Zeitangabe, oder ein anderer Wortlaut?
