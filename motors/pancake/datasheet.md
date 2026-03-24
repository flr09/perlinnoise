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
| Nennspannung | 2,4 | V | I×R Richtwert |
| **Nennstrom (berechnet)** | **~0,92** | **A** | 2,4 V / 2,6 Ω – wahrscheinlicher Dauerstrom |
| Max. Strom (AliExpress) | 1,88 | A | **Schmelzgrenze / Peak – nicht dauerhaft!** |
| **Betriebslimit (gesetzt)** | **1000** | **mA** | ~10 % über Nenn – sicheres Testlimit |
| **Empfohlener RMS-Strom** | **600** | **mA** | Startpunkt Tuning (FW-Default) |

> **Varianten-Hinweis:** Es gibt zwei Varianten der 36BYG1204:
> - **Low-R** (unser Motor): 2,6 Ω / 1,29 mH / ~0,92 A Nenn – hoher Strom, gute Dynamik
> - **High-R** (andere Variante): 13 Ω / 10 mH / 0,5 A Nenn – für andere Anwendungen
>
> Die im Netz kursierende "500 mA"-Empfehlung gilt für die **High-R-Variante** – nicht unseren Motor.
>
> **Zur Nennspannung:** Der TMC2209 regelt per PWM-Chopper. Die 2,4 V sind nur der I×R-Wert
> bei Nennstrom, keine Betriebsspannung. Mit 12 V Versorgung = bessere Hochdrehzahl-Performance.

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
| rms_current | 300 | 1000 | mA | **1000 mA = hartes Limit** (~10 % über Nennstrom) |
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
