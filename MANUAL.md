# E4 Simplex Shaper — Bedienungsanleitung

**Firmware:** v0.2.4 | **Board:** FYSETC E4 (ESP32 + TMC2209 × 4)

---

## 1. Hardware-Übersicht

| Komponente | Detail |
|---|---|
| Board | FYSETC E4 |
| MCU | ESP32 |
| Treiber | 4× TMC2209 (UART, Stealthchop) |
| Motoren | X, Y, Z, E (AccelStepper) |
| UART-Adressen | Z=0, X=1, E=2, Y=3 |
| Motorstrom | 600 mA RMS |
| Microsteps | 16 |
| Max. Speed | 8000 steps/s |
| Beschleunigung | 4000 steps/s² |
| Lüfter-Ausgang | BED-MOSFET (GPIO 13) |
| Lampe-Ausgang | HOTEND-MOSFET (GPIO 2) |
| Temperatursensor | GPIO 36 (analog, Thermistor) |

**Motor-Reihenfolge** in der UI: M1=X, M2=Y, M3=Z, M4=E.
Im Noise-Modus werden die Motoren nebeneinander im Noise-Raum abgetastet
(Abstand gesteuert per **Motor Spacing**-Slider).

---

## 2. Firmware hochladen (PlatformIO)

### Voraussetzung
- [VS Code](https://code.visualstudio.com/) + [PlatformIO-Extension](https://platformio.org/install/ide?install=vscode)
- Board per USB-C angeschlossen

### Upload
```bash
# Im Projektordner:
pio run --target upload

# Oder mit Port-Angabe (falls mehrere Geräte):
pio run --target upload --upload-port /dev/ttyUSB0   # Linux
pio run --target upload --upload-port COM3           # Windows
```

### Monitor (Serial-Ausgabe)
```bash
pio device monitor --baud 115200
```
Zeigt nach Boot IP-Adresse und Status der TMC-Treiber.

### OTA-Update (ohne USB, über WLAN)
OTA ist in dieser Firmware noch nicht aktiv. Workaround: USB-Kabel anschließen
oder LittleFS + OTA-Update in zukünftiger Version nachrüsten.

> **Tipp:** Das Board braucht beim Upload manchmal einen manuellen Reset.
> Taste auf der Platine drücken, oder GPIO0 kurz auf GND legen.

---

## 3. WLAN-Einrichtung / Erststart

### Schritt 1 – AP-Modus (Erststart oder kein WLAN gespeichert)
1. Board startet als WLAN-Accesspoint **`E4-Setup`** (kein Passwort)
2. Mit diesem WLAN verbinden (Handy oder PC)
3. Browser öffnet sich automatisch (Captive Portal) — falls nicht: `http://192.168.4.1`
4. Seite **E4 Wi-Fi Setup** erscheint
5. SSID und Passwort des Heim-WLANs eingeben → **Save & Connect**
6. Board startet neu und verbindet sich mit dem Heim-WLAN

### Schritt 2 – STA-Modus (nach erfolgreicher Verbindung)
- IP-Adresse wird im Serial-Monitor angezeigt: `http://192.168.x.x`
- mDNS aktiv: **`http://e4.local`** (funktioniert auf macOS, iOS, Windows 10+, Linux mit Avahi)
- Browser-Tab aufmachen → Oberfläche erscheint sofort

### WLAN-Zugangsdaten zurücksetzen
Zugangsdaten werden im NVS (Non-Volatile Storage) gespeichert.
Reset: NVS löschen per `Preferences.clear()` oder `nvs_flash_erase()` im Code.
Alternativ: Wi-Fi-Konfigurationsseite erneut aufrufen unter `http://e4.local/install`.

---

## 4. Weboberfläche — Bedienung

Die Oberfläche ist zweispaltig: **links** Visualisierung + Motoran­zeige, **rechts** alle Regler.

### Start / Stop
| Button | Funktion |
|---|---|
| **START SYSTEM** / **STOP SYSTEM** | Motoren starten / anhalten (mit Auslauf) |
| **ALIGN (SET 0°)** | Aktuelle Motorposition als Nullpunkt definieren |

---

## 5. Bewegungsmodi (Dropdown: Movement Pattern)

### Noise-Modi (Motoren folgen Simplex-Rauschfeld)

| Modus | Wert | Beschreibung |
|---|---|---|
| **LINEAR** | 0 | Kamera fliegt geradlinig durch den Noise-Raum. Richtung per **Angle**. |
| **CIRCLE** | 1 | Kamera kreist im Noise-Raum. Kreisradius per **Radius**. |
| **FIGURE 8** | 2 | Liegende Acht (Lissajous). Amplitude per **Radius**. |

Alle drei Modi abtasten den Simplex-Noise-Raum und wandeln den Noise-Wert
in einen Motorwinkel um. **Motor Spacing** bestimmt den räumlichen Abstand
der Messpunkte der vier Motoren voneinander (in cm).

### Wellenform-Modi (direkte Steuerfunktionen, kein Noise)

| Modus | Wert | Beschreibung |
|---|---|---|
| **SINUS** | 3 | Sinuswelle, stufenlos und rund. |
| **SAWTOOTH** | 4 | Sägezahn: langsam aufbauend, schnell zurück. |
| **SQUARE** | 5 | Rechteckwelle: sprunghaft zwischen zwei Extremen. |

Im Wellenform-Modus ist **Phasenversatz (°/Motor)** (früher: Motor Spacing)
der Abstand zwischen den Motoren in der Wellenphase:
- Wert 25 → 90° Versatz (Sinus sieht wie fließende Welle aus)
- Wert 50 → 180° Versatz (Motoren gegenphasig)
- Wert 0 → alle Motoren synchron

---

## 6. Alle Regler im Detail

### Noise-Modi (LINEAR / CIRCLE / FIGURE 8)

| Regler | ID | Bereich | Funktion |
|---|---|---|---|
| **Flight Speed** | speed | 0–2.0 | Geschwindigkeit der Kamerabewegung im Noise-Raum. Startwert: 0.12 |
| **Direction (Angle)** | angle | 0–360° | Flugrichtung (nur LINEAR). Startwert: 45° |
| **Path Radius** | rad | 1–500 | Kreisradius (CIRCLE / FIGURE 8) |
| **Framesize** | frame | 0.001–0.5 | Zoom ins Rauschfeld. Klein = große Hügel, Groß = fein. Startwert: 0.01 |
| **Contrast** | cont | 0–2.0 | Amplitudenskalierung der Motorwinkel. Startwert: 1.94 |
| **Form (Z-Shape)** | shape | -5 bis +5 | Kurvenform: 1=linear, >1=spitz/oben, <0=invertiert |
| **Edge Contrast** | edgec | 0–2.0 | Kantenverstärkung: 0=weich, 2=harte Spitzen. Startwert: 0.29 |
| **Max Range** | range | 30–360° | Maximaler Drehwinkel der Motoren |
| **Motor Spacing** | mspace | 5–100 cm | Abstand der Messpunkte der Motoren im Noise-Raum |
| **Map Zoom** | mapzoom | 0.2–3.0× | Zoom der Noise-Vorschau auf dem Canvas (nur visuell) |
| **PWM Fan** | fan | 0–100% | Lüftergeschwindigkeit (BED-Ausgang, GPIO 13) |
| **Lamp** | lamp | 0–100% | Lampenhelligkeit (HOTEND-Ausgang, GPIO 2) |

### Wellenform-Modi (SINUS / SAWTOOTH / SQUARE)

| Regler | Funktion im Wellenform-Modus |
|---|---|
| **Flight Speed** | Frequenz der Welle |
| **Phasenversatz** | Phasenabstand zwischen den 4 Motoren |
| **Contrast** | Amplitude (Motorhub) |
| **Max Range** | Maximaler Hub in Grad |
| **Sägezahn-Kurve** (SAWTOOTH) | Formfaktor: 0=linear, +5=exponentiell oben, -5=Wurzel |
| **Duty Cycle** (SQUARE) | Verhältnis HIGH zu LOW: 0=10%, 5=50%, 10=90% |
| **Flankenschärfe** | Weichheit der Übergänge per tanh: 0=sehr soft, 2=hart |
| **PWM Fan / Lamp** | unverändert |

---

## 7. Dynamics-Profil (Drive Dynamics)

Skaliert gleichzeitig Geschwindigkeit, Beschleunigung und Motorhub der Simulation.

| Wert | Profil | Speed | Accel | Hub |
|---|---|---|---|---|
| 0 | LANGSAM | ×0.60 | ×0.55 | ×0.75 |
| 1 | NORMAL | ×1.00 | ×1.00 | ×1.00 |
| 2 | RASANT | ×1.65 | ×1.85 | ×1.30 |

---

## 8. Preset-Slots (Speicher 1–8)

- **SPEICHERN-Taste** aktivieren (rot leuchtet = armed)
- Dann auf Slot-Nummer klicken → aktuell markierte Parameter werden gespeichert
- **Slot-Nummer klicken** (ohne SPEICHERN aktiv) → Slot laden

Welche Parameter gespeichert/geladen werden, bestimmen die kleinen
Buchstaben-Buttons (S/A/R/F/C/E/Z/G/M/...) links neben jedem Regler:
- Grün = wird gespeichert/geladen
- **ALL** = alle markieren / alle demarkieren

Presets werden im **LocalStorage** des Browsers gespeichert (nicht auf dem ESP).

---

## 9. Motoranzeige (Winkelkarten)

Unterhalb des Canvas: 4 Karten mit Winkelanzeige und Zeigernnadel für M1–M4.
Die Nadeln folgen den Ziel-Winkeln mit einer simulierten Motor-Trägheit
(Bang-Bang-Bremsweg-Steuerung, max. 720°/s, 1800°/s²).

---

## 10. Offline-Preview (edit.html)

`edit.html` ist eine eigenständige HTML-Datei im Projektordner — kein ESP32 nötig.

- Direkt im Browser öffnen (Doppelklick oder `file:///...`)
- Offline-Toggle oben rechts: **ON** = Vorschau mit Testwerten, **OFF** = versucht ESP zu erreichen
- Alle Modi (LINEAR, CIRCLE, FIGURE 8, SINUS, SAWTOOTH, SQUARE) vollständig funktionsfähig
- Enthält zusätzlich: Drive Dynamics-Slider, erweiterte Wellenform-Synthparameter

---

## 11. HTTP-Endpunkte (für Automatisierung / eigene Clients)

| URL | Methode | Beschreibung |
|---|---|---|
| `/` | GET | Weboberfläche |
| `/config` | GET | Aktuelle Konfiguration als JSON |
| `/set?param=wert` | GET | Parameter setzen (siehe Tabelle unten) |
| `/setzero` | GET | Alle Motorpositionen auf 0 setzen |
| `/install` | GET | WLAN-Setup-Seite |
| `/wifisave` | POST | WLAN-Zugangsdaten speichern (ssid, password) |

### `/set`-Parameter

| Parameter | Typ | Beispiel |
|---|---|---|
| `run` | 0/1 | `/set?run=1` |
| `type` | 0–5 | `/set?type=3` |
| `speed` | float | `/set?speed=0.12` |
| `angle` | float | `/set?angle=45` |
| `rad` | float | `/set?rad=50` |
| `range` | float | `/set?range=300` |
| `mspace` | float | `/set?mspace=25` |
| `frame` | float | `/set?frame=0.01` |
| `cont` | float | `/set?cont=1.94` |
| `shape` | float | `/set?shape=1.0` |
| `fan` | int | `/set?fan=128` |
| `lamp` | int | `/set?lamp=255` |

---

## 12. Tipps & Troubleshooting

| Problem | Lösung |
|---|---|
| Board taucht nicht im WLAN auf | Serial-Monitor öffnen → IP ablesen |
| `e4.local` nicht erreichbar | mDNS ggf. nicht unterstützt → direkte IP verwenden |
| Motoren bewegen sich nicht | `running=true` prüfen, ENABLE-Pin (GPIO 25) auf LOW prüfen |
| Motoren überhitzen | Motorstrom (aktuell 600 mA) im Code reduzieren |
| Upload schlägt fehl | Board-Reset drücken während PlatformIO „Connecting" zeigt |
| WLAN vergessen | `http://e4.local/install` aufrufen und neue Credentials eingeben |
| Browserfenster öffnet nicht | `http://192.168.4.1` manuell eintippen (AP-Modus) |
