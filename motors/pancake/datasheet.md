# Motor Datenblatt – Pancake Stepper (AliExpress)

---

## Identifikation

| Feld | Wert |
|------|------|
| Bezeichnung | Pancake Stepper NEMA17 (flach) |
| Hersteller | — (AliExpress OEM) |
| Modell / SKU | — |
| Quelle / Link | https://de.aliexpress.com/item/1005006423228157.html |
| Gehäuse-Typ | NEMA17 Pancake |
| Gewicht | — |

---

## Elektrische Parameter

| Parameter | Wert | Einheit | Hinweis |
|-----------|------|---------|---------|
| Phasen | 2 | — | Bipolar |
| Phasenwiderstand | 2,6 | Ω | Datenblatt |
| Phaseninduktivität | 1,29 | mH | Datenblatt |
| Nennspannung | 2,4 | V | I×R Richtwert (≈ 0,92 A × 2,6 Ω) |
| Max. Strom (Datenblatt) | 1,88 | A | **Schmelzgrenze – nicht dauerhaft!** |
| **Betriebslimit (gesetzt)** | **1200** | **mA** | Unser Soft-Limit |
| **Empfohlener RMS-Strom** | **600** | **mA** | Startpunkt Tuning (aktuell in FW) |

> **Zur Nennspannung:** Der TMC2209 regelt den Strom per PWM-Chopper.
> Mit 12 V Versorgung liegt der Duty-Cycle bei Stillstand bei ~26 %
> (3,12 V Nutzspannung bei 1,2 A × 2,6 Ω). Das ist korrekt und gewollt –
> höhere Versorgungsspannung = schnellerer Stromanstieg = bessere Hochdrehzahl-Performance.
> Die 2,4 V aus dem Datenblatt sind nur ein Richtwert bei Nennlast, **nicht** die Betriebsspannung.

---

## Mechanische Parameter

| Parameter | Wert | Einheit |
|-----------|------|---------|
| Schritte/Umdrehung | 200 | steps/rev (1,8°) |
| Mikroschritte (FW) | 16 | → 3200 steps/rev |
| Haltemoment | — | N·cm |
| Anzugmoment | — | N·cm |
| Trägheitsmoment Rotor | — | g·cm² |
| Achsdurchmesser | — | mm |

---

## Betriebsgrenzen für Auto-Tune

| Parameter | Min | Max | Einheit | Hinweis |
|-----------|-----|-----|---------|---------|
| rms_current | 300 | 1200 | mA | **1200 mA = hartes Limit** |
| Empf. Startpunkt | 600 | — | mA | Aktueller FW-Default |
| Max. RPM (erwartet) | — | ~800 | RPM | Schätzung, wird gemessen |
| SGTHRS Start | 50 | 100 | — | Startwert für SG-Lernlauf |

---

## Zeitkonstante & Chopper-Hinweise

```
τ = L / R = 1,29 mH / 2,6 Ω = 0,50 ms
```

- Stromanstiegszeit ~0,5 ms → oberhalb ~300 RPM beginnt Drehmomentabfall
- Mit 12 V Versorgung: Stromanstieg ca. 9,3 A/ms → ausreichend bis ~1000+ RPM
- Bei Pancake-Bauform: kurze Spulen, geringes Trägheitsmoment → gut für schnelle Richtungswechsel
- Verdacht: geringes Haltemoment bei niedriger Drehzahl durch niedrige Induktivität

---

## Notizen / Beobachtungen

- **2026-03-24:** Erster Parcour-Lauf. SG_RESULT extrem niedrig (0–46 statt ~400+).
  13 Stall-Events beim Drift-Check (200 sps). Parcour bricht nach 300 RPM ab.
  Ursache unklar: SGTHRS nicht gesetzt (Default=0), evtl. zu wenig Strom bei Langsamlauf.
  → Nächster Schritt: SGTHRS lernen, Strom auf 800–1000 mA anheben und neu testen.

---

## Parcour-Ergebnisse (wird vom System befüllt)

| Datum | FW | max RPM | max Accel | rms_current | SGTHRS | Notiz |
|-------|----|---------|-----------|-------------|--------|-------|
| 2026-03-24 | 3.2.0 | <300 | — | 600 mA | 0 (default) | Erster Lauf, Stall im Drift-Check |
