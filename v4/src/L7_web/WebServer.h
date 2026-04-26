#pragma once
#include <ESPAsyncWebServer.h>

// L7 — Web-Server-Setup
//
// In Phase 1 nur 3 Endpoints:
//   GET /         — minimale HTML-Seite "v4 Phase 1"
//   GET /status   — JSON: fw, ip, uptime, log, m[0]
//   GET /cmd      — Power-Toggle für Motor 0 (a=pwr&m=0)
//
// Spätere Phasen erweitern hier: /telemetry, /watchdog, /set, /config, /setzero,
// /install, /wifisave, /update (ElegantOTA), /pcnt.

namespace WebServer {

void begin();

} // namespace WebServer
