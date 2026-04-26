#include "Storage.h"
#include <Preferences.h>
#include "../L0_platform/Logger.h"

namespace Storage {

void init() {
    // Phase-1-Stub: triggert Lazy-Init des NVS-Subsystems durch ESP-IDF.
    Preferences p;
    p.begin("v4_meta", false);
    p.putUInt("phase", 1);
    p.end();
    Logger::addLog("NVS: ready");
}

} // namespace Storage
