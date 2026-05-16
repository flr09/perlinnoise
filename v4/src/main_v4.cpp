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
#include <ElegantOTA.h>

#include "L0_platform/Platform.h"
#include "L0_platform/Logger.h"
#include "L0_platform/Types.h"
#include "L1_hal/Hal_Pins.h"
#include "L1_hal/Hal_Pcnt.h"
#include "L1_hal/Hal_Tacho.h"
#include "L2_storage/Storage.h"
#include "L2_storage/Storage_Runtime.h"
#include "L3_driver/Tmc2209.h"
#include "L3_driver/Stepper.h"
#include "L4_mechanics/Calibration.h"
#include "L4_mechanics/Homing.h"
#include "L4_mechanics/SetZero.h"
#include "L5_programs/characterization/Characterization.h"
#include "L5_programs/synthesis/Synthesis.h"
#include "L6_telemetry_safety/OpState.h"
#include "L6_telemetry_safety/Telemetry.h"
#include "L6_telemetry_safety/Watchdog.h"
#include "L6_telemetry_safety/MotorProfile.h"
#include "L7_web/Wifi.h"
#include "L7_web/WebServer.h"

static void SynthesisTask(void*) {
    Logger::addLog("SynthesisTask: Core 1");
    for (;;) {
        Synthesis::tick();
        vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz Update
    }
}

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
            bool blockedBySynth = v4::rt.running;
            
            if (Op::pending.setzero >= 0) {
                int m = Op::pending.setzero; Op::pending.setzero = -1;
                if (blockedBySynth) { Logger::addLog("ERR: SetZero blockiert — Synth läuft"); }
                else { SetZero::apply(m); }
            }
            else if (Op::pending.home >= 0) {
                int m = Op::pending.home; Op::pending.home = -1;
                if (blockedBySynth) { Logger::addLog("ERR: Homing blockiert — Synth läuft"); }
                else {
                    Op::state = v4::OpState::HOMING;
                    Homing::run(m);
                    Op::state = v4::OpState::IDLE;
                }
            }
            else if (Op::pending.calib >= 0) {
                int m = Op::pending.calib; Op::pending.calib = -1;
                if (blockedBySynth) { Logger::addLog("ERR: Calib blockiert — Synth läuft"); }
                else {
                    Op::state = v4::OpState::CALIBRATING;
                    Calibration::run(m);
                    Op::state = v4::OpState::IDLE;
                }
            }
            else if (Op::pending.learn >= 0) {
                int m = Op::pending.learn; Op::pending.learn = -1;
                if (blockedBySynth) { Logger::addLog("ERR: SG-Learn blockiert — Synth läuft"); }
                else {
                    Op::state = v4::OpState::LEARNING;
                    Characterization::runSgLearn(m);
                    Op::state = v4::OpState::IDLE;
                }
            }
            else if (Op::pending.test >= 0) {
                int m = Op::pending.test; Op::pending.test = -1;
                if (blockedBySynth) { Logger::addLog("ERR: Test blockiert — Synth läuft"); }
                else {
                    Op::state = v4::OpState::TESTING;
                    Watchdog::enable(m, true);
                    switch (Op::pending.testProg) {
                        case 0: Characterization::runSpeedTest(m);   break;
                        case 1: Characterization::runInertiaTest(m); break;
                        case 2: Characterization::runCoastTest(m);   break;
                        case 3: Characterization::runKatapult(m);    break;
                        case 4: Characterization::runFreqSweep(m);   break;
                        case 5: Characterization::runCurrentSweepHiRPM(m); break;
                        case 6: Characterization::runTachoCutoffDiagnostic(m); break;
                        case 7: Characterization::runProfileTest(m); break;
                        case 8: Characterization::runSilentProfileTest(m); break;
                        default: Characterization::runSpeedTest(m);  break;
                    }
                    Watchdog::enable(m, false);
                    Op::state = v4::OpState::IDLE;
                }
            }
            else if (Op::pending.show >= 0) {
                int m = Op::pending.show; Op::pending.show = -1;
                if (blockedBySynth) { Logger::addLog("ERR: Show blockiert — Synth läuft"); }
                else {
                    Op::state = v4::OpState::SHOWING;
                    Telemetry::resetBuffer();
                    Characterization::runPerformanceShow(m);
                    Op::state = v4::OpState::IDLE;
                }
            }
        }

        // Stop-Trigger an aktive Stepper weiterreichen
        if (Op::pendingStop) {
            Synthesis::stop();
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
    HalTacho::init();           // L1 — Tacho-ISR ZUERST (v3-Erkenntnis: FAS überschreibt sonst Interrupt-Handler)
    HalPcnt::init();            // L1 — PCNT VOR FAS!
    Tmc::init();                // L3 — UART + ENABLE
    Stepper::init();            // L3 — FAS-Engine (4 Stepper)
    HalPcnt::initInputBuffers();// L1 — F15-Fix NACH FAS-Init
    HalTacho::reattach();       // L1 — Tacho-ISR nochmal NACH FAS, falls FAS Interrupts überschrieb

    Wifi::connectOrAP();        // L7
    ArduinoOTA.setHostname("perlin-v4");
    ArduinoOTA.begin();
    WebServer::begin();         // L7
    Telemetry::init();          // L6 — TMC-Poll-Task auf Core 0
    MotorProfileNs::loadAll();  // L6 — Profile aus NVS laden
    Watchdog::init();           // L6 — State zurücksetzen
    Watchdog::startTask();      // L6 — 50 Hz Watchdog-Task auf Core 0
    Synthesis::init();          // L5b — Bewegungs-Synthese-Pipeline

    Platform::startMovementTask(MovementTask, "Move", 8192, 1);
    Platform::startMovementTask(SynthesisTask, "Synth", 4096, 2); // Höhere Prio
}

void loop() {
    ArduinoOTA.handle();
    ElegantOTA.loop();   // ohne diesen Aufruf rebootet ElegantOTA nach Upload nicht
    // Bug-ID 23a: Throttled NVS-Persistence der RuntimeConfig. touch() im
    // /set-Handler markiert dirty + setzt Zeitstempel; tickFlush() schreibt
    // erst 5 s nach der letzten Slider-Änderung — schützt NVS vor Wear-Out.
    StorageRuntime::tickFlush(v4::rt);
    WebServer::tickReboot();   // Bug-ID 23c: deferred Reboot nach /wifisave|/wificlear
    delay(10);
}
