#include "Platform.h"
#include "Sync.h"
#include "Logger.h"

namespace Platform {

void init() {
    Serial.begin(115200);
    delay(50);
    Sync::init();
    Logger::clear();
    Logger::addLog(String("=== Boot v") + FW_VERSION + " ===");
}

void startMovementTask(MovementTaskFn fn, const char* name,
                       uint32_t stackBytes, UBaseType_t prio) {
    // Core 1 = App-CPU. WiFi-Stack ist auf Core 0 hardcoded — dadurch ist
    // Core 1 weitestgehend dem Movement gewidmet.
    xTaskCreatePinnedToCore(fn, name, stackBytes, nullptr, prio, nullptr, 1);
}

} // namespace Platform
