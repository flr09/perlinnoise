# Briefing für Claude Design — perlin v4 Web-UI

## Was ich brauche

Eine **funktionale, mobil-taugliche Web-UI** für eine ESP32-Motorsteuerung. Der Stil soll an **Bauhaus** angelehnt sein: form follows function, geometrische Klarheit, beschränkte Farbpalette, klare Typografie. **Nicht** nerdig-technisch (kein dunkler Hacker-Look mit grünem Monospace), sondern werkstatt-tauglich, gut lesbar bei Tageslicht.

## Wichtige Constraints

Diese Datei läuft auf einem **ESP32** als embedded HTML-String im Flash-Speicher. Das bedeutet:

1. **Kein externes Loading.** Keine Web-Fonts (Google Fonts, Adobe Fonts), keine externen JS-Bibliotheken (jQuery, Vue, React), keine Bilder. Alles inline in einer einzigen HTML-Datei.
2. **System-Font-Stack** verwenden: `-apple-system, BlinkMacSystemFont, "Segoe UI", Inter, "Helvetica Neue", Arial, sans-serif`.
3. **Footprint klein halten.** Aktuell ~6 KB HTML, gerne unter 12 KB bleiben.
4. **Keine rechenintensiven Animationen** — keine `backdrop-filter`, keine permanenten CSS-Transitions, keine Canvas-Animationen. Statische UI.
5. **Mobile-first.** Auch auf einem Smartphone in der Werkstatt bedienbar (Touch-Targets ≥ 44 × 44 px).
6. **Dark-Mode ist optional**, aber Bauhaus war traditionell hell — Off-White als Default ist gewünscht.

## Designsprache

**Bauhaus-Druckfarben** als Kernpalette:
- Off-White / warmes Papier: `#f4f1ea` (Hintergrund)
- Schwarz: `#1a1a1a` (Schrift, Linien)
- Rot: `#d62828` (aktive/wichtige Status)
- Blau: `#003049` (Info, sekundär)
- Gelb: `#fcbf49` (Akzent, Highlights)
- Grau: `#6b6b6b` (Mute / inaktiv)

Geometrische Elemente: Quadrate (Karten), Kreise (Status-Punkte), dünne schwarze Linien als Trenner. Keine abgerundeten Ecken übertreiben — Bauhaus war eckig.

Typografie: groß und klar. H1 ~1.6em fett. Labels klein und gesperrt (`text-transform: uppercase; letter-spacing: 0.1em`). Werte in `font-variant-numeric: tabular-nums` für saubere Tabellen.

## Was die UI können muss

### Inhalts-Sektionen (von oben nach unten)

1. **Header**
   - Titel „perlin v4"
   - Rechts: FW-Version + IP-Adresse (klein, sekundär)

2. **Status-Strip** (3 Zellen nebeneinander)
   - **OP**: aktueller Operations-Zustand (`idle`, `homing`, `calib`, `learn`, `test`, `show`)
   - **UPTIME**: Sekunden seit Boot
   - **RPM**: aktuelle Drehzahl (kann „—" sein)

3. **Motoren** (4 Karten in einem Grid — 1 Spalte mobile, 2 Spalten Desktop)
   - Pro Motor: Status-Punkt (Kreis, Farbe je nach Zustand), Bezeichnung (M·X / M·Y / M·Z / M·E), aktueller Zustand (z.B. „powered" oder „off" oder „—"), Aktions-Buttons.
   - Farbe Status-Punkt: `red` = powered, `weiß+Rand schwarz` = off, `weiß+gestrichelter Rand grau` = inaktiv/nicht verbunden.
   - Aktions-Buttons pro Motor (je nach Phase):
     - Phase 1+2: `Power`, `Set Zero`
     - Phase 2+: `Calib`, `Home` (nur wenn Sensor verfügbar — bei E nicht)
     - Phase 3+: `Test` startet Engineering-Programme
     - Phase 4+: `Synth` startet Bewegungs-Synthese

4. **Performance-Steuerung** (kommt erst Phase 4 — kann jetzt schon im Layout vorgesehen sein als „später")
   - Movement Pattern Dropdown (Linear, Circle, Figure 8, Sinus, Sawtooth, Square)
   - Slider für: Speed, Range, Frame, Contrast, Z-Shape, Motor Spacing
   - Drive Dynamics: Langsam / Normal / Rasant
   - START / STOP

5. **Log** (klein, am Ende)
   - Zeilenweise mit Zeitstempel
   - max. ~80 Zeilen
   - Bauhaus-Gelb als Akzent für den Zeitstempel-Pill

6. **Footer**
   - „perlin v4 · phase X" (klein, grau)

### JavaScript-Verhalten

- Polling alle **500 ms**: `GET /status` → JSON parsen → Felder aktualisieren
- Click-Aktion: `GET /cmd?a=<aktion>&m=<motor>` (z.B. `/cmd?a=pwr&m=2`)
- Keine externe Lib, nur vanilla JS, ~50–100 Zeilen

## API-Vertrag (zum Anbinden der UI)

### `GET /status` antwortet mit JSON:

```json
{
  "fw": "4.0.0-phase1",
  "uptime_s": 123,
  "ip": "192.168.193.22",
  "op": "idle",
  "rpm": null,
  "log": "Boot ok\nWiFi STA\n",
  "m": [
    { "e": false, "p": 0.0, "s": 0 },
    { "e": null,  "p": null, "s": null },
    { "e": false, "p": 0.0, "s": 0 },
    { "e": null,  "p": null, "s": null }
  ]
}
```

- `e` = enabled (true/false/null wenn Motor nicht aktiv)
- `p` = posDeg (Position in Grad)
- `s` = speed (steps per second)
- `op` = aktueller Operations-Zustand
- `rpm` = aktuelle Drehzahl (null wenn Motor steht oder nicht da)
- `log` = neue Log-Einträge seit letztem Polling, durch `\n` getrennt

### `GET /cmd?a=<aktion>&m=<motor>` Aktionen:

- `pwr` — Power toggeln
- `home` — Sensor-basiertes Homing (X/Y/Z)
- `cal` — Sensor-Kalibrierung (X/Y/Z)
- `setzero` — Aktuelle Position als 0° setzen (alle 4)
- `learn` — StallGuard-Profil lernen
- `test` — Engineering-Tests starten
- `show` — Vorführmodus
- `stop` — Emergency Stop
- `synth` — Bewegungs-Synthese starten (Phase 4+)

Antwortet mit `200 OK` oder `409 BUSY` (wenn andere Aktion läuft) oder `400/501` bei Fehler.

## Was der Code liefern muss

Eine einzelne `index_html` als C-String im PROGMEM, eingebettet in `WebServer.cpp`:

```cpp
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
...
</html>
)HTML";
```

Bei Sonderzeichen im HTML/JS auf Escape achten — der C++-Roh-String-Literal `R"HTML(...)HTML"` schützt vor `\n`/`"`-Problemen.

## Aktuelles HTML als Ausgangspunkt

Die aktuelle Version (von mir, dem Coding-Agent, selbst gemacht — funktioniert, aber kann besser) liegt nebenan in `current.html`. Sie zeigt schon das Grundprinzip der Bauhaus-Übertragung, aber dein Auftrag: **ordentlicher, eleganter, mit besseren Proportionen und gegebenenfalls einem klügeren Layout**. Du darfst alles umwerfen, aber bitte die API-Felder oben einhalten.

## Was ich NICHT will

- Keine Hero-Section mit großem Bild
- Keine Marketing-Sprache („Welcome to perlin v4!")
- Keine Tabs/Akkordeons mit JavaScript-Animation
- Keine Glasmorphismus-Effekte (`backdrop-filter`)
- Keine Schatten unter Karten
- Keine abgerundeten Ecken über 4–6 px
- Keine Tooltips mit verzögerter Einblendung
- Keine emoji-Icons in Buttons
- Keine Loading-Spinner (Polling ist genug)

## Phasen-Hinweis

Die UI wird sich noch entwickeln. **Phase 1** (jetzt) braucht nur Header + Status + Motor-Karten + Log. **Phase 2** kommt Cal/Home/SetZero hinzu. **Phase 3** kommt Test-Buttons. **Phase 4** kommt der Performance-Bereich (Slider/Pattern). Designsystem so anlegen, dass diese Erweiterungen gut reinpassen — z.B. ein klares Karten-Pattern, das man wiederverwenden kann.

---

Wenn du willst kannst du mir auch zwei Varianten liefern (z.B. „strict bauhaus" vs. „bauhaus-influenced contemporary"), dann wähle ich.
