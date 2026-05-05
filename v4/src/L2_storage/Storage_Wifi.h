#pragma once
#include <Arduino.h>

// L2 — Persistierung der WiFi-Credentials in NVS.
//
// Bug-ID 23c: WiFi-Credentials waren bisher hardcoded in `wifi_settings.h`
// (Klartext im Git-Verlauf, problematisch für Public-Push). Mit v4.2.0
// liegen sie primär im NVS — `wifi_settings.h` bleibt nur als Fallback
// für Erst-Inbetriebnahme.

namespace StorageWifi {

struct WifiCreds {
    char ssid[33];
    char pass[65];
    bool valid = false;
};

// Lädt Credentials aus NVS. out.valid=true wenn Eintrag existiert.
void load(WifiCreds& out);

// Speichert SSID + Passwort. Setzt valid=true.
void save(const String& ssid, const String& pass);

// Löscht den NVS-Eintrag. Beim nächsten Boot fällt das System auf den
// `wifi_settings.h`-Default zurück.
void clear();

} // namespace StorageWifi
