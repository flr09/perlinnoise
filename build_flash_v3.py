#!/usr/bin/env python3
"""
V3 Build & Flash Script
-----------------------
python build_flash_v3.py            -- nur bauen + .bin in v3/ ablegen
python build_flash_v3.py flash      -- bauen + USB COM4 flashen
python build_flash_v3.py ota        -- bauen + OTA flashen (perlin-v3.local)
"""

import subprocess
import shutil
import sys
import os
from datetime import datetime
from pathlib import Path

# --- Konfiguration ---
PROJECT_DIR = Path(__file__).parent
ENV         = "fysetc_e4_v3"
VERSION     = "3.3.0"
COM_PORT    = "COM4"

# PlatformIO via venv (WSL)
_venv_pio = PROJECT_DIR / ".venv" / "bin" / "python3"
PIO_CMD   = [str(_venv_pio), "-m", "platformio"] if _venv_pio.exists() else ["pio"]

BIN_SRC     = PROJECT_DIR / ".pio" / "build" / ENV / "firmware.bin"
V3_DIR      = PROJECT_DIR / "v3"

# -------------------------------------------------

def run(cmd, **kwargs):
    """subprocess.run mit Ausgabe, bricht bei Fehler ab."""
    result = subprocess.run(cmd, cwd=PROJECT_DIR, **kwargs)
    if result.returncode != 0:
        sys.exit(result.returncode)
    return result


def build():
    print(f"\n[BUILD] env={ENV} ...")
    run(PIO_CMD + ["run", "-e", ENV])
    print("[BUILD] OK")


def copy_bin():
    if not BIN_SRC.exists():
        print(f"[COPY]  Kein Binary gefunden: {BIN_SRC}")
        sys.exit(1)

    ts          = datetime.now().strftime("%Y%m%d_%H%M%S")
    dest_ts     = V3_DIR / f"firmware_v3_{VERSION}_{ts}.bin"
    dest_latest = V3_DIR / "firmware_v3_latest.bin"

    shutil.copy2(BIN_SRC, dest_ts)
    shutil.copy2(BIN_SRC, dest_latest)

    size_kb = dest_ts.stat().st_size / 1024
    print(f"[COPY]  {dest_ts.name}  ({size_kb:.1f} KB)")
    print(f"[COPY]  firmware_v3_latest.bin aktualisiert")
    print(f"\n  Windows-Pfad: \\\\wsl.localhost\\Ubuntu{str(dest_ts).replace('/', chr(92))}")


def flash_usb():
    print(f"\n[FLASH] USB {COM_PORT} ...")
    run(PIO_CMD + ["run", "-e", ENV, "--upload-port", COM_PORT, "-t", "upload"])
    print("[FLASH] Fertig - Board startet neu.")


def flash_ota():
    print(f"\n[OTA]   perlin-v3.local ...")
    run(PIO_CMD + ["run", "-e", f"{ENV}_ota", "-t", "upload"])
    print("[OTA]   Fertig - Board startet neu.")


# -------------------------------------------------

if __name__ == "__main__":
    mode = sys.argv[1].lower() if len(sys.argv) > 1 else "build"

    build()
    copy_bin()

    if mode == "flash":
        flash_usb()
    elif mode == "ota":
        flash_ota()
    else:
        print("\n[INFO]  Nur gebaut. Flashen mit:")
        print(f"          python build_flash_v3.py flash   (USB {COM_PORT})")
        print(f"          python build_flash_v3.py ota     (WLAN OTA)")
