# FSD — PerlinNoise v4 (Modular)

**Stand:** 2026-05-17 — Firmware **v4.4.32** auf Hardware. Phase 10.1–10.3 abgeschlossen, Phase 10.4 (API-First) in Planung.
**Branch:** `v4-modular`
**Status:** Systemstabilität gehärtet. Range-Konvention auf Peak-to-Peak (pp) vereinheitlicht. Dynamics-Profile (Slow/Normal/Rasant) aktiv.

---

## 9. Phasen-Plan (Fortsetzung)

### Phase 10.1 — Stabilitäts- & Safety-Sicherung (v4.4.8)
*Abgeschlossen.* (Recorder-Streaming, Watchdog-Adaptive, Calib-Schutz).

### Phase 10.2 — Silent Mode Optimization (v4.4.16)
*Abgeschlossen.* (Silent-Matrix, Dynamisches MS-Scaling).

### Phase 10.3 — GUI-Härtung & Dynamics (v4.4.23 → v4.4.32)

**Ziel:** Player-Crashes beheben, Slider-Ehrlichkeit, Dynamics-Profile.

**Meilensteine:**
- **v4.4.29:** Log-Flood gestoppt (`drainBuffer`), DOM-Thrashing in `/test` behoben.
- **v4.4.30:** Saw-Refinement (Lineare Rampe + Sharp Retract via `zShape`), Passiver Slider-Limiter (nur `.max` Update).
- **v4.4.31:** Dynamics-Profile (Slow: 256MS/Silent, Normal: 64MS, Rasant: 16MS/Full-Power).
- **v4.4.32 (Critical Sync):** Range-Konvention auf **Peak-to-Peak** umgestellt (Bug 68/87). Engine rechnet jetzt `amplitude = range / 2.0`. UI-Labels und physikalische Fahrt sind nun synchron.

### Phase 10.4 — API-First Architecture & Local Control Tool (v4.4.33+)

**Ziel:** Entlastung des ESP32 durch Auslagerung der UI auf das lokale Dateisystem (`file://`).

#### 1. Logik & Transport
*   **Datei:** `v4/tools/PerlinControl_v4.html` (Standalone, enthält INDEX + TEST Features).
*   **Transport:** REST-Polling (10Hz Status, 10Hz Preview). WebSocket/SSE bleibt Backlog für v4.5.
*   **IP-Sync:** Konfigurierbare Ziel-IP in der UI, Speicherung im `localStorage`.

#### 2. Logistik: ESP32 Refactoring
*   **HTML Stripping:** Entfernen von `INDEX_HTML` und `TEST_HTML` (~40 KB Ersparnis).
*   **CORS Hardening:**
    - Header `Access-Control-Allow-Origin: *` auf allen API-Endpoints.
    - Globaler **OPTIONS-Handler** für Preflight-Requests (HTTP 204 No Content).
    - Explizite `Allow-Methods: GET, POST, OPTIONS` und `Allow-Headers: Content-Type`.
*   **Security-Note:** CSRF-Schutz wird für dieses interne Engineering-Tool zugunsten der Usability (file:// Origin) bewusst ausgesetzt.

#### 3. Engineering UI (`TEST_HTML`)
*   Das Engineering-Tool wird in `PerlinControl.html` als separater Tab integriert. Kein separates Stripping nötig, da alles in einer lokalen Datei lebt.

---

### Phase 10.5 — Mobile & Small Display Optimization (Backlog)

**Ziel:** Bedienbarkeit auf Smartphones via Media-Queries und einklappbaren UI-Elementen.
