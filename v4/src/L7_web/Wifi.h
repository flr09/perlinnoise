#pragma once
#include <Arduino.h>

// L7 — WiFi
//
// Versucht STA mit Credentials aus wifi_settings.h. Bei Fehlschlag: AP-Fallback
// `perlin-v4-setup` (offen, kein PW). mDNS-Hostname `perlin-v4`.
// In Phase 6 wird das durch Storage_Wifi (NVS) ersetzt.

namespace Wifi {

void connectOrAP();
bool isConnected();
String localIp();

} // namespace Wifi
