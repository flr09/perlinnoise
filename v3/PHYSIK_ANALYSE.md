# Physikalische Grenzen & Microstep-Strategie

**Stand:** 2026-03-26 (aktualisiert nach KATAPULT-Lauf) | **Motor:** NEMA 17, TMC2209, FYSETC E4

---

## 1. Gemessene Grenzen (aus Telemetrie)

### Geschwindigkeit
| Wert | RPM | SPS (16MS) | Status |
|------|-----|-----------|--------|
| Verifiziertes Maximum | 2500 RPM | 133.333 SPS | ✅ 0 Stalls |
| SG_RESULT bei 2500 RPM | — | — | **22** (dünn, aber stabil) |
| SG_RESULT bei 2000 RPM | — | — | 130 (komfortable Reserve) |
| Empfohlenes Dauermaximum | ~2300 RPM | ~122.667 SPS | SG ~60, gute Reserve |

> **SG_RESULT = 22 bei 2500 RPM:** Unterhalb von SGTHRS=50, aber kein Stall ausgelöst,
> weil der Motor in SpreadCycle-Mode läuft (TPWMTHRS-Grenze bereits überschritten) und
> StallGuard dort anders interpretiert wird. Für kurze Positionswechsel OK.
> Für Dauerlauf (>10 Sek.) wäre 2300 RPM sicherer.

### Beschleunigung (Stand: runInertiaTest getestet bis 40.000 sps²)
| Beschleunigung | Revs zum Hochlauf auf 2500 RPM (16MS) | Revs zum Stopp |
|----------------|---------------------------------------|----------------|
| 2.000 sps² | **2.963 Umdrehungen** | 2.963 |
| 30.000 sps² | **197 Umdrehungen** | 197 |
| 40.000 sps² | **148 Umdrehungen** | 148 |
| 100.000 sps² | **59 Umdrehungen** | 59 |
| 500.000 sps² | **12 Umdrehungen** | 12 |

Formel: `Umdrehungen = v² / (2 × a × stepsPerRev)`
Bei 16MS: `stepsPerRev = 3200`, `v = 133.333 SPS`

> **Fazit:** Kurze Rampen (< 10 Umdrehungen) bei 2500 RPM sind mit normalen 16MS-Einstellungen
> nicht erreichbar. Hier kommt die Microstep-Reduktion ins Spiel (→ Abschnitt 3).

---

## 2. Coast-Test & Bremsweg

Der `runCoastTest` misst über 1 Sekunde bei 400 RPM die Tachoimpulse.
Der Bremsweg eines freilaufenden Motors hängt von:
- Trägheitsmoment der Last (Rotor + angehängte Mechanik)
- Reibung (Lager, Getriebe)
- Back-EMF des Motors (wirkt bremsend)

**Für harte Positionswechsel:**
- Aktives Abbremsen (FastAccelStepper `stopMove()` → Rampe) ist kontrolliert
- Freilauf (toff=0, Motor kraftlos) → kürzerer mechanischer Bremsweg, aber Positionsverlust möglich
- Optimum: Abbremsen mit maximaler Rampe bis ~100 SPS, dann Freilauf → Position halten

**Offene Messung:** Der Bremsweg von 2500 RPM mit a=40.000 sps² beträgt ~148 Umdrehungen —
das ist zu lang für abrupte Stopps. Mit a=200.000 sps² wären es ~30 Umdrehungen.
Physikalisch möglich? → Muss gemessen werden (stallGuard als Indikator).

---

## 3. Microstep-Strategie

### Warum Microstep-Reduktion für schnelle Moves?

Das TMC2209 interpoliert intern immer auf 256 Microsteps → **die Bewegungsqualität
bleibt gleich, unabhängig vom MRES-Register**. Der ESP sieht nur weniger Schritte.

| MRES | SPS bei 2500 RPM | Revs zum Hochlauf (a=100k sps²) | Praktisch? |
|------|-----------------|----------------------------------|------------|
| 64 MS | 533.333 SPS | **2.370 Umdrehungen** | ❌ Zu lang |
| 16 MS | 133.333 SPS | **149 Umdrehungen** | ❌ Zu lang |
| 8 MS  | 66.667 SPS | **37 Umdrehungen** | 🟡 Grenzwertig |
| 4 MS  | 33.333 SPS | **9 Umdrehungen** | ✅ Kurzrampe möglich |
| 2 MS  | 16.667 SPS | **2,3 Umdrehungen** | ✅ Sehr kurz |
| 1 MS  | 8.333 SPS | **0,6 Umdrehungen** | ✅ Fast instantan |

> **Schlüsselerkenntnis:** Bei 1 MS und a=173.000 sps² ist 2500 RPM
> in **einer Umdrehung** erreichbar. Mit TMC2209-Interpolation keine Qualitätsverluste.

### Wann ist der Wechsel möglich?

**TMC2209 Einschränkung:** MRES kann jederzeit geändert werden — der Treiber
übernimmt die neue Auflösung beim nächsten Schritt. **Kein** Positionssprung durch
interne 256MS-Interpolation.

**FastAccelStepper Einschränkung:** `setMicrosteps()` im Code skaliert `currentPosition`
mit dem Faktor (neu/alt). Muss aufgerufen werden **wenn der Motor steht** oder
mit manueller Positions-Korrektur.

**Empfohlene Strategie (noch nicht implementiert):**
```
Parkposition / Idle:          64 MS  — leise, präzise
Mittlere Moves (<500 RPM):    16 MS  — aktueller Default
Schnelle Moves (>500 RPM):     4 MS  — kurze Rampen
Maximale Beschleunigung:       1 MS  — Instantan-Moves
```

Wechselpunkt: Motor unter ~50 SPS (= fast stehend) → MRES wechseln → neu beschleunigen.

---

## 4. Offene Messaufgaben

| # | Test | Zweck | Status |
|---|------|-------|--------|
| T1 | `runInertiaTest` mit neuem Code (E1-Fix) | Reales a_max messen | 🕒 Ausstehend |
| T2 | Coast bei 2500 RPM (KATAPULT Phase C verbessert) | Bremsweg-Basis mit realer Spinup-Zeit | 🕒 Ausstehend |
| T3 | Parcour 200–2500 RPM vollständig | SG-Profil über gesamten Bereich | 🕒 Ausstehend |
| T4 | KATAPULT Phase A — Hunt-Fix (Start ≥600 RPM) | Reale Stall-Grenze pro MS-Stufe | 🕒 Nach v3.6.10 |
| T5 | Ghost-Mode mit belasteter Welle | Minimum Betriebsstrom unter Last | 🕒 Ausstehend |

---

## 5. Empfehlung für nächsten Parcour

Basierend auf allen bisherigen Telemetrie-Daten:

```
Zielgeschwindigkeit: 2000 RPM (verifiziert stabil, SG 100–120)
Beschleunigung:      100.000 sps² (Phase B bestätigt, kein Stall)
Microsteps:          16 MS (Phase A Hunt erst nach Fix nutzbar)
Bremsrampe:          symmetrisch (100k sps²)
Ghost-Dauerbetrieb:  ≥100 mA empfohlen (unter Last; 50 mA nur Freilauf)
```

---

## 6. v3.6.9 — KATAPULT & Fixes (2026-03-26)

### Bug-Fix: Kalibrierung CCW-Rückpass Timeout
- **Problem:** Nach dem CW-Pass fährt der Motor 0,8 Umdrehungen vorwärts, dann CCW zurück.
  Bei 400 sps und 3200 Schritten (0,8 rev @ 16MS) dauert das **6,4 Sekunden** — Timeout war 5000 ms → Abbruch vor Sensor.
- **Fix:** Timeout 5000 ms → **12000 ms** in beiden Präzisionspässen (CW + CCW).

### Tacho-RPM Anzeige
- ISR misst Zeit zwischen aufeinanderfolgenden LOW-Flanken (= 1 Umdrehung)
- `getTachoRpm()` gibt 0 zurück wenn Motor >2 Sek. stillsteht
- Web-UI zeigt RPM-Feld live neben Speed-SPS

### KATAPULT-Modus (3 Phasen)
```
Phase A — Stall-Hunt:
  MS-Stufen [32,16,8,4,2,1] nacheinander
  Pro Stufe: RPM-Rampe 200→2500 in 200er Schritten (SpreadCycle)
  Stop wenn SG_RESULT < 20 (Stall-Grenze)
  → Ergebnis: bestMS + bestRPM für Phase B

Phase B — Full-Load Launch (3×CW + 3×CCW):
  bestMS aus Phase A, a=100.000 sps², 2 Sek. pro Run
  Telemetrie: "LAUNCH_CW" / "LAUNCH_CCW"

Phase C — Coast + Ghost Mode:
  Coast: toff=0 (Motor kraftlos), Tacho misst Auslauf 3 Sek.
  Ghost: StealthChop (en_spreadCycle=false, TPWMTHRS=0 → immer leise)
         Strom-Sweep 400→50 mA in 50er Schritten
         Pro Stufe: 1,5 Sek. bei 400 RPM, Tacho-Verifikation
         → Minimum Betriebsstrom unter StealthChop
```

### Ghost-Mode Technik-Detail
- `TPWMTHRS = 0` erzwingt StealthChop bei **allen** Geschwindigkeiten
  (Bedingung für SpreadCycle: TSTEP ≤ TPWMTHRS; mit TPWMTHRS=0 niemals erfüllt)
- `en_spreadCycle(false)` deaktiviert SpreadCycle-Override zusätzlich
- Minimum-Strom-Verifikation: erwartet ≥70 % der Soll-Umdrehungen (10 rev in 1,5 s @ 400 RPM)

---

## 7. KATAPULT-Erstlauf: Messergebnisse & Analyse (2026-03-26)

**Datei:** `parcour_2026-03-26T06-12-38.csv` | **Firmware:** v3.6.9

### Phase A — Stall-Hunt: Bug identifiziert

Alle 6 MS-Stufen stallen sofort bei 200 RPM mit SG = 2–12:

| MS | SPS | RPM | SG_RESULT | Bewertung |
|----|-----|-----|-----------|-----------|
| 32 | 21333 | 200 | 12 | Abbruch (< 20) |
| 16 | 10666 | 200 | 2 | Abbruch |
| 8 | 5333 | 200 | 2 | Abbruch |
| 4 | 2665 | 200 | 2 | Abbruch |
| 2 | 1333 | 200 | 2 | Abbruch |
| 1 | 666 | 200 | 2 | Abbruch |

**Ursache:** SG_RESULT ist bei niedrigen Drehzahlen prinzipbedingt unzuverlässig. Der TMC2209
liefert sinnvolle StallGuard-Werte erst oberhalb von `TCOOLTHRS` (hier ≈50 RPM-Äquivalent in
TSTEP). Bei 200 RPM ist das Drehmomentmuster der Spulen zu schwach um einen Unterschied zu
einem echten Stall zu machen — SG fällt auf nahe 0, obwohl der Motor frei dreht.

**Folge im Code:** Fallback greift korrekt — `bestMs=16`, `bestAbsRpm=2000 RPM` (hartcodiert).

**Fix für v3.6.10:**
- Hunt erst ab **≥600 RPM** starten (sicher über TCOOLTHRS)
- Alternativ: SG erst nach ≥500ms bei konstanter Geschwindigkeit samplen (nicht direkt nach Settle)
- Threshold 20 bleibt, aber nur gültig wenn `cs_actual > 0` (Strom fließt wirklich)

---

### Phase B — Full-Load Launch 3×CW + 3×CCW: Voller Erfolg

**Konfiguration:** bestMs=16 (Fallback), 2000 RPM, a=100.000 sps²

**Beschleunigungsrampe (gemessen):**
- 0 → 106.666 SPS in **≈1100 ms** ✅ (theoretisch: 106.666/100.000 = 1067 ms)
- Kein Stall in keinem der 6 Runs

**SG-Werte im eingeschwungenen Zustand (≈2000 RPM):**

| Richtung | SG_RESULT (Steady-State) | cs_actual | stall |
|----------|--------------------------|-----------|-------|
| CW Run 1 | 100–118 | 28 | 0 |
| CW Run 2 | 100–120 | 28 | 0 |
| CW Run 3 | 100–108 | 28 | 0 |
| CCW Run 1 | 96–220 | 28 | 0 |
| CCW Run 2 | 96–206 | 28 | 0 |
| CCW Run 3 | 100–170 | 28 | 0 |

**SG während CCW-Hochlauf:** bis **506** (nahe am Maximum 511) — Motor hat bei 100k sps²
Beschleunigung maximales Drehmoment, StallGuard sieht volle Reserve. Kein Stall. ✅

**Vergleich mit altem Parcour-Test (30k sps²):**
- Alter Test @ 2000 RPM: SG ≈ 96–120
- Phase B @ 2000 RPM: SG ≈ 100–118
- **Identisch** — 100k sps² Beschleunigung verschlechtert den Steady-State nicht. ✅

**Fazit Phase B:** a=100.000 sps² bei 2000 RPM ist **produktionsreif**. Die aggressivere
Rampe ist für kurze Positionswechsel geeignet, ohne SG-Reserve zu opfern.

---

### Phase C — Coast: Schnelles Abbremsen bestätigt

**Beobachtung:** Erster Tacho-Wert nach `toff=0`: **4080 RPM** (17 Pulse in 250ms).
Ab zweitem Sample: **0 RPM** — Motor physisch gestoppt.

**Interpretation:**
Der Spinup (delay=2000ms, a=30k sps²) erreicht nur ~1125 RPM (30k×2s = 60k SPS ≠ Ziel 106k).
Der erste Coast-Wert 4080 RPM ist rechnerisch zu hoch — wahrscheinliche Ursache ist die
**Richtungsumkehr**: Phase B endet CCW, Phase C läuft CW. Der physische Motor kann durch
Massenträgheit + entgegenkommende CW-Schritte kurzzeitig schneller werden (Überschwingen).

**Kernerkenntnis:** Der Motor bremst nach `toff=0` in **unter 500ms** auf Stillstand — durch:
- Back-EMF (wirkt als Bremse bei offenen Spulen)
- Cogging-Torque (magnetische Rastmomente)
- Lagerreibung

`olb=1` (Open Load Phase B) ab Stillstand: **erwartetes Verhalten** bei `toff=0`. ✅

**Fix für v3.6.10:** Spinup-Zeit von 2s auf **≥4s** erhöhen, damit echte Zielgeschwindigkeit
erreicht wird bevor Freilauf beginnt. Pulszähler-Reset nach dem Spinup beibehalten.

---

### Phase C — Ghost Mode (StealthChop): Überraschend niedrig

**Konfiguration:** StealthChop erzwungen (`TPWMTHRS=0`), 400 RPM, Strom-Sweep 400→50 mA

| Strom | SG | cs_actual | ola | Ergebnis |
|-------|----|-----------|-----|----------|
| 400 mA | 62 | 12 | 1 | ✅ OK |
| 350 mA | 14 | 10 | 0 | ✅ OK |
| 300 mA | 32 | 8 | 1 | ✅ OK |
| 250 mA | 74 | 7 | 1 | ✅ OK |
| 200 mA | 106 | 5 | 1 | ✅ OK |
| 150 mA | 64 | 3 | 1 | ✅ OK |
| 100 mA | 70 | 2 | 1 | ✅ OK |
| **50 mA** | 100 | **0** | 1 | ✅ OK |

**Motor läuft bei 50 mA / cs=0 durch alle 1,5s-Fenster** — sehr bemerkenswert.

**Interpretation der Grenzwerte:**
- `cs_actual = 0` bei 50 mA: TMC2209 liefert nahezu keinen Strom mehr.
- `ola = 1` (Open Load) ab 400 mA schon aktiv: Bei StealthChop + sehr niedrigem Strom
  erkennt der Treiber die Spule als "offen" — kein echter Fehler, sondern Artefakt der
  Strommessung bei minimaler PWM-Einschaltzeit.
- **spd=0 in allen Zeilen**: Telemetrie wird nach `stopMove()` geschrieben — korrekt,
  FAS-Geschwindigkeit ist dann 0. Die physische Drehung findet vorher statt. ✅

**Einschränkung:** Bei 50 mA dreht der Motor **durch Massenträgheit**, nicht durch echtes
Drehmoment. Für einen **unbelasteten** Freilauf (z. B. Schleife auf einer Rolle) könnte 50 mA
ausreichen. Für Positionierung unter Last empfohlen: **≥150 mA** (cs≥3, ola=0 konsistent).

**SG in StealthChop:** Nicht für Stall-Detektion verwertbar — andere Physik als SpreadCycle.
Die hohen SG-Werte (62–106) sind kein Widerspruch zu niedrigem cs_actual.

---

### Bekannte Bugs & geplante Fixes (→ v3.6.10)

| ID | Symptom | Ursache | Fix |
|----|---------|---------|-----|
| A1 | Phase A stalled bei allen MS @ 200 RPM | SG unter TCOOLTHRS unbrauchbar | Hunt ab ≥600 RPM starten |
| A2 | SG-Threshold 20 zu aggressiv | Niedrig-RPM SG prinzipbedingt ~0 | SG nur auswerten wenn `cs_actual > 0` |
| C1 | Spinup erreicht nur ~1125 RPM statt 2000 | delay=2000ms + a=30k sps² zu langsam | Spinup-Zeit auf ≥4s (oder a auf ≥60k sps²) |
| G1 | Ghost-Verifikation bei cs=0/ola=1 nicht aussagekräftig | Motor dreht durch Trägheit, nicht Strom | Loop abbrechen wenn `ola=1` bei mehreren Samples |
