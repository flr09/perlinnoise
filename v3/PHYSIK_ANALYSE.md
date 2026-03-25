# Physikalische Grenzen & Microstep-Strategie

**Stand:** 2026-03-26 | **Motor:** NEMA 17, TMC2209, FYSETC E4

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
| T2 | `runCoastTest` bei 2500 RPM | Bremsweg-Basis | 🕒 Ausstehend |
| T3 | Parcour 200–2500 RPM vollständig | SG-Profil über gesamten Bereich | 🕒 Ausstehend (maxRpm zurücksetzen) |
| T4 | Microstep-Wechsel bei laufendem Motor | Resonanz/Stall beim Wechsel? | 🕒 Ausstehend |
| T5 | a_max bei 1MS/4MS (Stall-Grenze) | Physikalisches Limit | 🕒 Ausstehend |

---

## 5. Empfehlung für nächsten Parcour

Basierend auf den Telemetrie-Daten:

```
Zielgeschwindigkeit: 2300 RPM (sicherer SG-Buffer)
Beschleunigung:      100.000 sps² (testen — T1)
Microsteps:          16 MS (aktuell) → 4 MS für schnelle Segmente (T4 erst)
Bremsrampe:          symmetrisch zur Beschleunigungsrampe
```
