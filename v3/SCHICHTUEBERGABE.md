# Schichtübergabe — 2026-03-26

**Session:** v3.6.x PCNT-Experiment + Doku-Aufräumen
**Letzte verifiziert lauffähige FW:** `firmware_v3_3.5.4_20260324_181741.bin` ✅
**Aktuelle FW:** v3.6.2 — gebaut, **noch nicht geflasht**, Funktion unbekannt

---

## Aktueller Stand auf einen Blick

```
v3.5.4  ← LETZTE BEKANNTE GUTE VERSION (getestet 2026-03-25)
  ↓
v3.6.0  ← runSpeedTest-Fix (Beschleunigung) — nicht geflasht/getestet
  ↓
v3.6.1  ← PCNT eingebaut → GPIO-Konflikt → System hing (defekt)
  ↓
v3.6.2  ← GPIO-Fix (INPUT_OUTPUT) → gebaut, NICHT GETESTET
```

---

## Was in v3.5.4 verifiziert wurde (2026-03-25)

Telemetrie: `parcour_2026-03-25T21-59-29.csv`

| Metrik | Ergebnis |
|--------|----------|
| RPM-Ziel-Range | 200–2500 RPM (24 Stufen) |
| Stall-Events | **0** |
| cs_actual | **28 konstant** (= 900mA, Boost hatte unnötig gefeuert wegen E1/E2) |
| Laufzeit | 24 Sekunden |
| Dropouts | keine |

**Caveat:** Motor hat nie die Ziel-RPM tatsächlich erreicht (E1/E2-Bug, erst in v3.6.0 gefixt). Er lief immer noch auf der Beschleunigungsrampe. Physisch aber gesund und stabil.

---

## Was in v3.6.0 geändert wurde (nicht getestet)

| Bug | Fix |
|-----|-----|
| E1: Kein `setAcceleration()` in `runSpeedTest` → erbt 2000 sps² | `setAcceleration(30000)` am Anfang |
| E2: 500ms Settle blind | Aktives Warten auf `getCurrentSpeedInMilliHz() >= 95%` |
| E3: Boost feuerte bei jedem Schritt | Boost prüft jetzt `reachedRpm` aus Speed-Register |

---

## Was in v3.6.1/v3.6.2 versucht wurde (PCNT)

**Ziel:** ISR-sichere Positionserfassung in `characterizeSensor` via ESP32 PCNT-Hardware.

**v3.6.1 — defekt:**
`pcnt_unit_config()` setzt GPIO 27 intern auf Input → RMT-Output (FastAccelStepper) blockiert → Motor dreht sich nicht → System hängt.

**v3.6.2 — Fix:**
```cpp
pcnt_unit_config(&pcnt_cfg);
gpio_set_direction((gpio_num_t)X_STEP, GPIO_MODE_INPUT_OUTPUT);  // ← Restore
gpio_set_direction((gpio_num_t)X_DIR,  GPIO_MODE_INPUT_OUTPUT);
```
ESP32 GPIO-Matrix kann einen Pin gleichzeitig als Output (RMT) und Input (PCNT) betreiben. Fix ist technisch korrekt — aber noch **nicht auf Hardware verifiziert**.

---

## Nächste Schritte (in Reihenfolge)

### Option A — Schnell: v3.5.4 flashen und mit v3.6.0 weiterarbeiten

Falls v3.6.2 auf Hardware nicht startet → auf v3.5.4 zurückfallen und v3.6.0 separat testen.

```
# v3.5.4 (verifiziert):
firmware_v3_3.5.4_20260324_181741.bin

# v3.6.0 (E1/E2/E3-Fix, kein PCNT):
firmware_v3_3.6.0_20260325_231030.bin
```

### Option B — v3.6.2 flashen und testen

```
python build_flash_v3.py ota
# oder manuell: firmware_v3_3.6.2_20260325_235628.bin
```

**Testsequenz v3.6.2:**
1. `POWER ON` → LED muss grün werden, keine Panic im seriellen Monitor
2. `CALIB SENSOR` → Neue Log-Zeilen prüfen:
   ```
   CW: XXXX-XXXX (XX steps)
   CCW: XXXX-XXXX (XX steps)
   Center: XXXX
   ```
   Wenn CW-Breite negativ oder > 500 steps → PCNT-Richtung invertiert (lctrl/hctrl tauschen)
3. `HOME MOTOR` → Motor muss zur 0°-Position fahren
4. `START PARCOUR` → Läuft jetzt länger (~2–3 min) wegen korrekter Acceleration-Settle-Logik

### Falls v3.6.2 hängt

PCNT-Block komplett entfernen und `characterizeSensor` auf v3.5.4-Stand zurücksetzen. PCNT ist eine Verbesserung, aber keine Notwendigkeit — v3.5.4 läuft korrekt ohne PCNT.

---

## Datei-Referenz

| Datei | FW | Datum | Status |
|-------|----|-------|--------|
| `v3/tele/parcour_2026-03-25T21-59-29.csv` | 3.5.4 | 2026-03-25 | ✅ Verifiziert, 0 Stalls |
| `v3/firmware_v3_3.5.4_20260324_181741.bin` | 3.5.4 | 2026-03-24 | ✅ Letzte bekannte gute Version |
| `v3/firmware_v3_3.6.0_20260325_231030.bin` | 3.6.0 | 2026-03-25 | 🟡 E1/E2/E3-Fix, kein PCNT, nicht getestet |
| `v3/firmware_v3_3.6.2_20260325_235628.bin` | 3.6.2 | 2026-03-25 | 🟡 PCNT+GPIO-Fix, nicht getestet |
| `v3/firmware_v3_latest.bin` | 3.6.2 | 2026-03-25 | 🟡 = 3.6.2 |

---

## Referenz-Schwellwerte (aus v3.5.4-Lauf)

| Metrik | Ist-Wert (v3.5.4) | Ziel (nach v3.6.0-Fix) |
|--------|-------------------|------------------------|
| Max getestetes RPM-Ziel | 2500 RPM | 2500 RPM (diesmal tatsächlich erreicht) |
| Tatsächlich erreichte RPM | ~35% (Rampen-Bug) | ~100% (E1/E2 gefixt) |
| Stall-Events | 0 | 0 |
| cs_actual | 28 (900mA, Boost-Bug) | 20–26 (650mA, kein unnötiger Boost) |
| Laufzeit Parcour | 24s (Rampe nie abgeschlossen) | ~2–3 min |
| CALIB SENSOR Genauigkeit | ±2–3 Schritte (Polling-Latenz) | <1 Schritt (PCNT, wenn v3.6.2 läuft) |
