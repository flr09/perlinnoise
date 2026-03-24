# Motor Datenblatt – TEMPLATE

> Kopiere diese Datei in den entsprechenden Unterordner und fülle die Felder aus.
> Leerfelder mit `—` markieren wenn unbekannt.

---

## Identifikation

| Feld | Wert |
|------|------|
| Bezeichnung | |
| Hersteller | |
| Modell / SKU | |
| Quelle / Link | |
| Gehäuse-Typ | z.B. NEMA17 Pancake / NEMA17 Standard / NEMA23 |
| Gewicht | g |

---

## Elektrische Parameter

| Parameter | Wert | Einheit | Hinweis |
|-----------|------|---------|---------|
| Phasen | 2 | — | Bipolar |
| Phasenwiderstand | | Ω | Gemessen oder Datenblatt |
| Phaseninduktivität | | mH | |
| Nennspannung | | V | I×R bei Nennstrom |
| Nennstrom (RMS) | | A | Dauerbetrieb |
| Spitzenstrom (Peak) | | A | Kurzzeitig |
| **Betriebslimit (gesetzt)** | | A | Unser Soft-Limit im Firmware |
| **Empfohlener RMS-Strom** | | A | Startpunkt für Tuning |

---

## Mechanische Parameter

| Parameter | Wert | Einheit |
|-----------|------|---------|
| Schritte/Umdrehung | 200 | steps/rev (1,8°) |
| Haltemoment | | N·cm |
| Anzugmoment | | N·cm |
| Trägheitsmoment Rotor | | g·cm² |
| Achsdurchmesser | | mm |
| Länge | | mm |

---

## Betriebsgrenzen für Auto-Tune

| Parameter | Min | Max | Einheit | Hinweis |
|-----------|-----|-----|---------|---------|
| rms_current | | | mA | Absolutes Limit: niemals überschreiten |
| Empf. Startpunkt | | | mA | Für ersten Parcour |
| Max. RPM (erwartet) | | | RPM | Grober Richtwert |
| SGTHRS Start | 50 | 100 | — | Startwert für SG-Lernlauf |

---

## Zeitkonstante & Chopper-Hinweise

```
τ = L / R = [Induktivität] mH / [Widerstand] Ω = [Ergebnis] ms
```

- Höhere Versorgungsspannung → schnellerer Stromanstieg → bessere Hochdrehzahl-Performance
- TMC2209 regelt Strom via PWM – Nennspannung des Motors ist irrelevant, nur Strom zählt
- Richtwert Chopper-Frequenz: 35–55 kHz (Standard TMCStepper)

---

## Notizen / Beobachtungen

_Freitextfeld für Erfahrungen, Auffälligkeiten, getestete Konfigurationen_

---

## Parcour-Ergebnisse (wird vom System befüllt)

| Datum | FW | max RPM | max Accel | rms_current | SGTHRS | Notiz |
|-------|----|---------|-----------|-------------|--------|-------|
| | | | | | | |
