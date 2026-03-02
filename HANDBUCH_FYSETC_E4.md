# Handbuch: PerlinNoise auf FYSETC E4 (ESP32) flashen

Stand: 2026-02-09  
Projektordner: `/home/chris/perlinnoise`  
Firmware-Umgebung laut `platformio.ini`: `env:fysetc_e4` mit `board = esp32dev`

## 1) Voraussetzungen

1. USB-Datenkabel (kein reines Ladekabel).
2. PlatformIO CLI installiert.
3. Unter Linux: Benutzer in `dialout` Gruppe.

### PlatformIO CLI installieren (Linux)

```bash
python3 -m pip install --user platformio
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
platformio --version
```

Falls Serial-Port Rechte fehlen:

```bash
sudo usermod -aG dialout "$USER"
```

Danach einmal ab- und wieder anmelden.

## 2) USB-Port des Boards finden

Board einstecken, dann:

```bash
ls -l /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

Typisch ist z. B. `/dev/ttyUSB0` oder `/dev/ttyACM0`.

## 3) Firmware bauen

```bash
cd /home/chris/perlinnoise
platformio run -e fysetc_e4
```

## 4) Firmware aufspielen

`/dev/ttyUSB0` ggf. durch deinen Port ersetzen:

```bash
cd /home/chris/perlinnoise
platformio run -e fysetc_e4 -t upload --upload-port /dev/ttyUSB0
```

Wenn beim Upload ein Timeout kommt (ESP32 Bootloader nicht getroffen):

1. `BOOT` gedrückt halten.
2. `EN`/`RST` kurz drücken.
3. `BOOT` loslassen, sobald Upload startet.

## 5) Seriellen Monitor öffnen

```bash
cd /home/chris/perlinnoise
platformio device monitor -b 115200 -p /dev/ttyUSB0
```

Du solltest Startmeldungen wie `E4 Simplex Shaper Starting` sehen.

## 6) WLAN-Setup nach dem Flash

Die Firmware startet so:

1. Versucht gespeichertes WLAN (`ssid/pass`) zu nutzen.
2. Falls nichts gespeichert oder Verbindung fehlschlägt: AP  
   SSID: `E4-SETUP`  
   Passwort: `12345678`

Dann:

1. Mit Handy/Laptop auf `E4-SETUP` verbinden.
2. Browser öffnen: `http://192.168.4.1/install`
3. Heim-WLAN eintragen und speichern.
4. Board startet neu und verbindet sich mit dem Router.
5. IP-Adresse im Serial Monitor ablesen.

Haupt-UI:

```text
http://<board-ip>/
```

## 7) Nützliche API-Endpunkte

- `GET /` -> Hauptoberfläche
- `GET /install` -> WLAN Setup
- `POST /wifisave` -> WLAN speichern
- `GET /set?...` -> Laufzeitparameter setzen
- `GET /setzero` -> aktuelle Position als Null setzen
- `GET /config` -> aktuelle Konfiguration als JSON

## 8) Häufige Fehler

### `platformio: command not found`
PlatformIO CLI ist nicht im `PATH`. Schritte aus Abschnitt 1 wiederholen.

### `Permission denied: /dev/ttyUSB0`
`dialout` Rechte fehlen. `usermod` aus Abschnitt 1 ausführen und neu anmelden.

### `A fatal error occurred: Failed to connect to ESP32`
BOOT/EN Sequenz aus Abschnitt 4 durchführen oder anderes USB-Kabel testen.

### Kein `/dev/ttyUSB*` oder `/dev/ttyACM*`
USB-Kabel tauschen, anderen Port probieren, Board-Stromversorgung prüfen.
