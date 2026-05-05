#include "Wifi.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "../L0_platform/Logger.h"
#include "../L2_storage/Storage_Wifi.h"
#include "wifi_settings.h"  // DEFAULT_SSID, DEFAULT_PASS — Fallback

namespace Wifi {

static constexpr const char* HOSTNAME = "perlin-v4";

void connectOrAP() {
    WiFi.setHostname(HOSTNAME);

    // Bug-ID 23c: erst NVS-Creds versuchen, sonst Fallback auf wifi_settings.h.
    StorageWifi::WifiCreds c;
    StorageWifi::load(c);
    const char* ssid;
    const char* pass;
    if (c.valid && c.ssid[0]) {
        ssid = c.ssid;
        pass = c.pass;
        Logger::addLog(String("WiFi: STA-Connect (NVS) SSID=") + ssid);
    } else {
        ssid = DEFAULT_SSID;
        pass = DEFAULT_PASS;
        Logger::addLog(String("WiFi: STA-Connect (Fallback) SSID=") + ssid);
    }
    WiFi.begin(ssid, pass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        MDNS.begin(HOSTNAME);
        Logger::addLog(String("WiFi: STA ") + WiFi.localIP().toString());
    } else {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("perlin-v4-setup");  // offen, kein PW (FSD: kein Default-PW im AP-Mode)
        Logger::addLog(String("WiFi: AP ") + WiFi.softAPIP().toString());
    }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

String localIp() {
    return isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
}

} // namespace Wifi
