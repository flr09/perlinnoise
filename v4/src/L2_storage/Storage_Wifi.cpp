#include "Storage_Wifi.h"
#include <Preferences.h>
#include <string.h>
#include "../L0_platform/Logger.h"

namespace StorageWifi {

static constexpr const char* NS = "wifi";

void load(WifiCreds& out) {
    out.valid = false;
    out.ssid[0] = '\0';
    out.pass[0] = '\0';
    Preferences p;
    p.begin(NS, true);
    if (p.getBytesLength("creds") == sizeof(WifiCreds)) {
        p.getBytes("creds", &out, sizeof(WifiCreds));
    }
    p.end();
}

void save(const String& ssid, const String& pass) {
    Preferences p;
    p.begin(NS, false);
    WifiCreds c;
    memset(&c, 0, sizeof(WifiCreds));
    strncpy(c.ssid, ssid.c_str(), sizeof(c.ssid) - 1);
    strncpy(c.pass, pass.c_str(), sizeof(c.pass) - 1);
    c.valid = true;
    p.putBytes("creds", &c, sizeof(WifiCreds));
    p.end();
    Logger::addLog(String("WIFI: NVS-Creds gespeichert (SSID=") + c.ssid + ")");
}

void clear() {
    Preferences p;
    p.begin(NS, false);
    p.remove("creds");
    p.end();
    Logger::addLog("WIFI: NVS-Creds gelöscht — Fallback wifi_settings.h beim nächsten Boot");
}

} // namespace StorageWifi
