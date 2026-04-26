#include "Wifi.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "../L0_platform/Logger.h"
#include "wifi_settings.h"  // DEFAULT_SSID, DEFAULT_PASS — gitignored

namespace Wifi {

static constexpr const char* HOSTNAME = "perlin-v4";

void connectOrAP() {
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);

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
