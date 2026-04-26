// PerlinNoise v4 — Eintrittspunkt
//
// Strikte Layer-Reihenfolge im setup():
//   L0 Plattform → L2 Storage → L1/L3 Hardware → L7 Web
//
// In Phase 1 ist die MovementTask noch leer — sie wird in Phase 2 (L4 Mechanik)
// mit Inhalt gefüllt. Das Pinning auf Core 1 ist aber jetzt schon gesetzt,
// damit die Architekturentscheidung sichtbar im Code steht.

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "L0_platform/Platform.h"
#include "L0_platform/Logger.h"
#include "L2_storage/Storage.h"
#include "L3_driver/Tmc2209.h"
#include "L7_web/Wifi.h"
#include "L7_web/WebServer.h"

// Phase-1-Stub: leerer Movement-Loop auf Core 1.
// Wird in Phase 2 (L4) durch die echte State-Machine ersetzt.
static void MovementTask(void*) {
    Logger::addLog("MovementTask: started on Core 1");
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Platform::init();
    Storage::init();

    Tmc::init();              // L3 Treiber-Init (Motor X)

    Wifi::connectOrAP();      // L7 WiFi
    ArduinoOTA.setHostname("perlin-v4");
    ArduinoOTA.begin();
    WebServer::begin();       // L7 HTTP

    Platform::startMovementTask(MovementTask);
}

void loop() {
    ArduinoOTA.handle();
    delay(10);
}
