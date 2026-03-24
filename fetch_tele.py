#!/usr/bin/env python3
"""
Telemetrie vom ESP holen und in v3/tele/ speichern.

    python3 fetch_tele.py              -- holt von perlin-v3.local
    python3 fetch_tele.py 192.168.x.x  -- holt von IP
"""

import sys
import urllib.request
from datetime import datetime
from pathlib import Path

HOST   = sys.argv[1] if len(sys.argv) > 1 else "perlin-v3.local"
URL    = f"http://{HOST}/telemetry"
OUTDIR = Path(__file__).parent / "v3" / "tele"

OUTDIR.mkdir(parents=True, exist_ok=True)

print(f"[FETCH] {URL} ...")
try:
    with urllib.request.urlopen(URL, timeout=10) as resp:
        if resp.status == 204:
            print("[INFO]  Keine Daten – erst Parcour laufen lassen.")
            sys.exit(0)
        data = resp.read()
except Exception as e:
    print(f"[ERR]   {e}")
    sys.exit(1)

ts       = datetime.now().strftime("%Y%m%d_%H%M%S")
outfile  = OUTDIR / f"tele_{ts}.csv"
outfile.write_bytes(data)

size_kb = len(data) / 1024
print(f"[OK]    {outfile.name}  ({size_kb:.1f} KB)")
print(f"\n  Windows-Pfad: \\\\wsl.localhost\\Ubuntu{str(outfile).replace('/', chr(92))}")
