// PerlinNoise v4 — Eintrittspunkt (Phase 2)
//
// Init-Reihenfolge ist kritisch (siehe v3 Bug F6/F15):
//   L0 → L2 → HalPcnt::init (PCNT vor FAS!) → Tmc::init → Stepper::init
//      → HalPcnt::initInputBuffers (FUN_IE NACH FAS-Init) → HalTacho::init
//      → L7 (WiFi/Web) → MovementTask
//
// MovementTask läuft auf Core 1 (App-CPU) — pollt Op::pending-Flags und
// dispatcht zu L4-Mechanik. State-Machine via Op::state.

#include <Arduino.h>
#include <ArduinoOTA.h>

#include "L0_platform/Platform.h"
#include "L0_platform/Logger.h"
#include "L0_platform/Types.h"
#include "L1_hal/Hal_Pins.h"
#include "L1_hal/Hal_Pcnt.h"
#include "L1_hal/Hal_Tacho.h"
#include "L2_storage/Storage.h"
#include "L3_driver/Tmc2209.h"
#include "L3_driver/Stepper.h"
#include "L4_mechanics/Calibration.h"
#include "L4_mechanics/Homing.h"
#include "L4_mechanics/SetZero.h"
#include "L6_telemetry_safety/OpState.h"
#include "L7_web/Wifi.h"
#include "L7_web/WebServer.h"

static void MovementTask(void*) {
    Logger::addLog("MovementTask: Core 1");
    for (;;) {
        // STOP und Power dürfen jederzeit
        if (Op::pending.power >= 0) {
            int m = Op::pending.power;
            Op::pending.power = -1;
            Tmc::setPower(m, Op::pending.powerOn[m]);
        }

        // Op-Aktionen nur im Idle (Interlock)
        if (Op::state == v4::OpState::IDLE) {
            if (Op::pending.setzero >= 0) {
                int m = Op::pending.setzero; Op::pending.setzero = -1;
                SetZero::apply(m);
            }
            else if (Op::pending.home >= 0) {
                int m = Op::pending.home; Op::pending.home = -1;
                Op::state = v4::OpState::HOMING;
                Homing::run(m);
                Op::state = v4::OpState::IDLE;
            }
            else if (Op::pending.calib >= 0) {
                int m = Op::pending.calib; Op::pending.calib = -1;
                Op::state = v4::OpState::CALIBRATING;
                Calibration::run(m);
                Op::state = v4::OpState::IDLE;
            }
        }

        // Stop-Trigger an aktive Stepper weiterreichen
        if (Op::pendingStop) {
            for (uint8_t i = 0; i < 4; i++) {
                auto* s = Stepper::get(i);
                if (s && s->isRunning()) s->stopMove();
            }
            Op::pendingStop = false;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup() {
    Platform::init();           // L0
    Storage::init();            // L2
    HalPcnt::init();            // L1 — PCNT VOR FAS!
    Tmc::init();                // L3 — UART + ENABLE
    Stepper::init();            // L3 — FAS-Engine (4 Stepper)
    HalPcnt::initInputBuffers();// L1 — F15-Fix NACH FAS-Init
    HalTacho::init();           // L1 — ISR für X/Y/Z

    Wifi::connectOrAP();        // L7
    ArduinoOTA.setHostname("perlin-v4");
    ArduinoOTA.begin();
    WebServer::begin();         // L7

    Platform::startMovementTask(MovementTask);
}

void loop() {
    ArduinoOTA.handle();
    delay(10);
}
