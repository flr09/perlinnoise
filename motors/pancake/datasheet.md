# Motor Datenblatt – 36BYG1204-A-6QHT (NEMA14 Pancake)

**Stand:** 2026-03-24 | **Recherche:** AliExpress-Listing + Orbiter-Referenz + Wantai-Vergleich

---

## Identifikation

| Feld | Wert |
|------|------|
| Bezeichnung | 36BYG1204-A-6QHT |
| Gehäuse-Typ | **NEMA14** (rund, 36 mm) – **kein NEMA17!** |
| Typischer Einsatz | Extruder (Voron Orbiter, Sherpa) |
| Quelle | https://de.aliexpress.com/item/1005006423228157.html |
| Referenz-Äquivalent | LDO-36STH20-1004AHG (Orbiter v2.0 Standardmotor) |

---

## ⚠️ Wichtige Varianten-Warnung

Unter der Bezeichnung „36BYG1204" oder „6BYG1204" werden zwei **vollständig verschiedene Motoren** verkauft:

| | **Variante A – unser Motor** | Variante B – Wantai 36HS2418 |
|---|---|---|
| Phasenwiderstand | **2,6 Ω** | ~1,9 Ω |
| Phaseninduktivität | **1,29 mH** | ~1–2 mH |
| Nennspannung | **2,4 V** | ~3,5 V |
| Nennstrom (RMS) | **~0,92 A** | **1,88 A** |
| Verlustleistung bei Nenn | **~4,4 W** | ~13,4 W |

> **Die 1,88 A auf der AliExpress-Seite gehören zu Variante B** und sind für unseren Motor
> **lebensgefährlich**: Bei 1,88 A würde Variante A **18,3 W** pro zwei Phasen verbraten (4× Nennlast) → sofortiger Überhitzungsschaden.

---

## Elektrische Parameter (Variante A – unser Motor)

| Parameter | Wert | Einheit | Hinweis |
|-----------|------|---------|---------|
| Phasen | 2 | — | Bipolar |
| Phasenwiderstand | 2,6 | Ω | Gemessen / Datenblatt |
| Phaseninduktivität | 1,29 | mH | Datenblatt |
| Nennspannung | 2,4 | V | I×R Richtwert |
| **Nennstrom (RMS)** | **~0,92** | **A** | 2,4 V / 2,6 Ω = 0,923 A |
| Spitzenstrom (Peak) | ~1,30 | A | I_RMS × √2 |
| **Betriebslimit (FW)** | **900** | **mA** | Hartes Soft-Limit (~10 % unter Nenn) |
| **Empfohlener RMS-Strom** | **650–850** | **mA** | Orbiter-Empfehlung: 0,85 A RMS |
| Verlustleistung @ 0,85 A | ~3,75 | W | gesamt (2 Phasen) |

---

## Mechanische Parameter

| Parameter | Wert | Einheit |
|-----------|------|---------|
| Schritte/Umdrehung | 200 | steps/rev (1,8°) |
| Mikroschritte (FW) | 16 | → 3200 steps/rev |
| Haltemoment (ca.) | 100–120 | mN·m |
| Gehäuse | NEMA14 rund, 36 mm Ø | |

---

## Betriebsgrenzen für Auto-Tune

| Parameter | Min | Max | Einheit | Hinweis |
|-----------|-----|-----|---------|---------|
| rms_current | 300 | **900** | mA | **900 mA = absolutes Soft-Limit** |
| FW-Startpunkt | 650 | — | mA | Orbiter-Referenz: 0,85 A |
| Max. RPM (erwartet) | — | ~600–800 | RPM | Schätzung, wird gemessen |
| SGTHRS Start | 50 | 100 | — | Startwert, wird via LEARN optimiert |

---

## Zeitkonstante & Chopper

```
τ = L / R = 1,29 mH / 2,6 Ω = 0,50 ms
Stromanstieg bei 12 V: ΔI/Δt = V/L = 12 / 0,00129 = ~9.300 A/s = 9,3 A/ms
→ Zielstrom 0,85 A in ~0,09 ms → gut für hohe Drehzahlen
```

- Duty-Cycle bei Stillstand (0,85 A, 2,6 Ω): 2,21 V / 12 V ≈ **18 %**
- TMC2209 Chopper regelt sauber, Nennspannung des Motors irrelevant

---

## Vergleich mit LDO-Referenzmotor (Orbiter v2.0)

| | Unser Motor | LDO-36STH20-1004AHG |
|---|---|---|
| Widerstand | 2,6 Ω | 2,1 Ω |
| Nennstrom | ~0,92 A | 1,0 A |
| Orbiter-Empfehlung | **0,85 A RMS** | 0,85 A RMS |
| → TMC rms_current | **650–850 mA** | 850 mA |

---

## Notizen / Beobachtungen

- **2026-03-24, Lauf 1:** Parcour bricht bei 300 RPM ab. SG_RESULT 0–46 (sehr niedrig).
  13 Stall-Events im Drift-Check (200 sps). SGTHRS=0 (Default). Strom=600 mA.
  → Ursache: zu wenig Strom für Langsamlauf + SGTHRS nicht gesetzt.
  → Nächster Schritt: LEARN SG PROFILE, dann Parcour mit 650 mA.

---

## Parcour-Ergebnisse

| Datum | FW | max RPM | max Accel | rms_current | SGTHRS | Notiz |
|-------|----|---------|-----------|-------------|--------|-------|
| 2026-03-24 | 3.2.0 | <300 | — | 600 mA | 0 | Erster Lauf, Stall im Drift-Check |
