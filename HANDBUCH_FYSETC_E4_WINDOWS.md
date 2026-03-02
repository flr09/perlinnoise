# Handbuch: PerlinNoise auf FYSETC E4 flashen (Windows)

Stand: 2026-02-09  
Projektordner: `C:\Users\<DEIN_USER>\perlinnoise`  
PlatformIO-Environment: `fysetc_e4` (`board = esp32dev`)

## 1) Voraussetzungen

1. USB-Datenkabel (kein reines Ladekabel)
2. Python 3 installiert (`python --version`)
3. Optional: Treiber fuer USB-Seriell-Chip (CH340 oder CP210x), falls Board nicht erkannt wird

## 2) PlatformIO CLI installieren (PowerShell)

```powershell
python -m pip install --user platformio
python -m platformio --version
```

Wenn `platformio` nicht direkt gefunden wird, nutze immer:

```powershell
python -m platformio <BEFEHL>
```

## 3) COM-Port vom Board finden

Board per USB einstecken, dann:

```powershell
mode
```

Oder im Geraete-Manager unter `Anschluesse (COM & LPT)` nachsehen (z. B. `COM5`).

## 4) Firmware bauen

```powershell
cd C:\Users\<DEIN_USER>\perlinnoise
python -m platformio run -e fysetc_e4
```

## 5) Firmware hochladen

`COM5` ggf. durch deinen Port ersetzen:

```powershell
cd C:\Users\<DEIN_USER>\perlinnoise
python -m platformio run -e fysetc_e4 -t upload --upload-port COM5
```

Wenn der Upload nicht startet:

1. `BOOT` gedrueckt halten
2. `EN`/`RST` kurz druecken
3. `BOOT` loslassen, sobald Upload beginnt

## 6) Seriellen Monitor oeffnen

```powershell
cd C:\Users\<DEIN_USER>\perlinnoise
python -m platformio device monitor -b 115200 -p COM5
```

Erwartet wird u. a. `E4 Simplex Shaper Starting`.

## 7) WLAN-Setup nach dem Flash

Firmware-Verhalten:

1. Verbindet sich mit gespeichertem WLAN (falls vorhanden)
2. Sonst startet Setup-AP:
   - SSID: `E4-SETUP`
   - Passwort: `12345678`

Dann:

1. Mit Laptop/Handy auf `E4-SETUP` verbinden
2. Browser: `http://192.168.4.1/install`
3. Heim-WLAN eintragen und speichern
4. Board startet neu
5. IP-Adresse im seriellen Monitor ablesen

Haupt-UI:

```text
http://<board-ip>/
```

## 8) Typische Fehler

### `No module named platformio`
PlatformIO noch nicht installiert oder falscher Python-Interpreter.

### `could not open port 'COMx'`
Falscher Port, Port belegt (anderer Monitor offen) oder Treiber fehlt.

### `Failed to connect to ESP32`
BOOT/EN-Sequenz nutzen, anderes USB-Kabel testen, anderen USB-Port pruefen.

### Board taucht nicht als COM-Port auf
Treiber installieren (CH340/CP210x), dann USB neu verbinden.
