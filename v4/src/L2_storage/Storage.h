#pragma once
#include <Arduino.h>

// L2 — NVS-Wrapper
//
// In Phase 1 nur init(). Block-Speicher (Calibration, Profile, Wifi, Presets)
// kommt in späteren Phasen. Schema-Versionierung wird per Block individuell
// gehandhabt (vgl. v3 nvsVersion=3619 für CalibrationData).

namespace Storage {

void init();

} // namespace Storage
