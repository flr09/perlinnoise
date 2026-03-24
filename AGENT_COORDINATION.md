# 📌 AGENT COORDINATION HUB (Post-its)

## 🕒 Aktueller Status (LIVE)
- **Fokus:** Jitter-Behebung & Tacho-Stabilität bei hohen RPM (v3.3.5).
- **Aktion:** Hardware-Interrupts (ISR) aktiv, Versions-Sprung auf 3.3.5.
- **Status:** Code bereit für Build. UART-Abfragen im Motor-Loop auf 500ms gedrosselt.

---

## 💬 Letzte Nachrichten (Chat)
- **Gemini (2026-03-24 12:15):** Massiver Schrittverlust in Telemetrie v3.2.0 erkannt (Soll 300 RPM -> Ist ~15 RPM). Ursache: UART-Polling blockiert Loop.
- **Lösung v3.3.3:** Umstieg auf Interrupts (ISR) fängt Tacho-Signal unabhängig von Loop-Latenz ein. Telemetrie-Cache eingeführt.
- **User-Hinweis:** Keine neuen .md Dateien! Bestehende Doku/Hubs pflegen.

---

## 🛠️ Wichtige Erkenntnisse (Shared Knowledge)
- **Interrupts:** `tachoISR` auf IRAM_ATTR gesetzt für maximale Geschwindigkeit.
- **Jitter:** `AccelStepper::run()` braucht Mikrosekunden-Präzision. Jede `driver.read()` Operation zerstört das Timing.
- **RPM-Ziel:** 800 RPM (42.666 sps). Aktuell wird v3.3.3 gebaut, um dieses Ziel erstmals real zu erreichen.

---

## 📝 Nächste Übergabe (To-Do für den nächsten Agenten)
1. **v3.3.3 Build** abschließen und flashen.
2. **Drift-Check** beobachten: Zeigt das Log nun realistische Drift-Werte (nahe 0) bei 300+ RPM?
3. **Telemetrie-Validierung:** Prüfen, ob die 50ms-Datenpunkte ohne 5s-Lücken ankommen.
