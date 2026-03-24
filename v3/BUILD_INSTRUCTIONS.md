# Build-Prozess & BIN-Generierung (v3.1.6)

Diese Dokumentation beschreibt für nachfolgende Agenten (z.B. Claude), wie die Firmware-Binärdateien in diesem Projekt generiert werden.

## 1. Werkzeuge
Das Projekt nutzt **PlatformIO (PIO)** innerhalb einer virtuellen Python-Umgebung im WSL (Ubuntu).
- **Pfad zu PIO:** `/home/chris/perlinnoise/.venv/bin/platformio`
- **Konfigurationsdatei:** `platformio.ini` im Projekt-Root.

## 2. Projekt-Struktur & Filter
Um Code-Kontamination zwischen Version 2 und Version 3 zu vermeiden, nutzt die `platformio.ini` den `build_src_filter`. 
- Die V3-Sourcen liegen exklusiv in `v3/src/`.
- Die V2-Sourcen liegen exklusiv in `v2/src/`.

## 3. Build-Befehl (CLI)
Um die aktuelle V3-Firmware zu bauen, wird folgender Befehl ausgeführt:

```bash
# Auszuführen im Verzeichnis /home/chris/perlinnoise/
/home/chris/perlinnoise/.venv/bin/python3 -m platformio run -e fysetc_e4_v3
```

## 4. Speicherorte der Artefakte
PlatformIO generiert die Dateien standardmäßig in einem versteckten Verzeichnis:
- **ELF-Datei:** `.pio/build/fysetc_e4_v3/firmware.elf`
- **BIN-Datei:** `.pio/build/fysetc_e4_v3/firmware.bin` (Dies ist die Datei für den Browser-Upload).

## 5. Release-Workflow
Nach einem erfolgreichen Build wird die `firmware.bin` manuell in den `v3`-Ordner kopiert und umbenannt, um Versionierung und Farbe (z.B. "Blue Update") zu kennzeichnen:

```bash
cp .pio/build/fysetc_e4_v3/firmware.bin v3/BLUE_RELEASE_V3_1_6.bin
```

## 6. Upload-Methoden
1. **Web-Interface (ElegantOTA):** Über `http://[IP-ADRESSE]/update` im Browser.
2. **PlatformIO OTA:** Über `pio run -e fysetc_e4_v3_ota -t upload` (nutzt Port 3232).
