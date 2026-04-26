#pragma once

// WiFi credentials — NICHT in git einchecken!
// Datei lokal befüllen, wifi_settings.h ist in .gitignore gelistet.
//
// Alternativ: Credentials über NVS setzen (empfohlen für Produktion):
//   prefs.begin("wifi", false);
//   prefs.putString("ssid", "MyNetwork");
//   prefs.putString("pass", "MyPassword");
//   prefs.end();

#define DEFAULT_SSID  "DEIN_NETZWERK"
#define DEFAULT_PASS  "DEIN_PASSWORT"
