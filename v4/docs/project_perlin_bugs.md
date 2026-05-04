# 🔴 Project PerlinNoise — Bug Log

Legend: 🔴 bug | 🟡 minor/refactor | ✅ fixed | 🔵 discovery

| ID | Date | Type | Title | Status |
|---|---|---|---|---|
| 1 | 2026-05-04 | 🔴 | **Calibration-Skip Logic Error (Decel-Bias)** | 🔴 open |
| 2 | 2026-05-02 | 🔴 | **ElegantOTA Reboot missing loop()** | ✅ v4.1.3 |
| 3 | 2026-05-04 | 🟡 | **NoiseEngine Redundancy (Synthesis.cpp duplication)** | 🟡 backlog |
| 4 | 2026-05-04 | 🟡 | **Tacho ISR Dead Code (Polling preferred)** | 🟡 backlog |
| 5 | 2026-05-04 | 🔵 | **Z-Motor Sweet-Spot > Hard-Limit (1000mA vs 900mA)** | 🔵 noted |

---

### [ID 1] Calibration-Skip Logic Error
- **Symptom:** Calib-Skip fails even when mechanics are unchanged. Second run is as slow as the first.
- **Root Cause:** In `Calibration.cpp`, `p2Pos` is captured after `stopMove()` and `waitWhileRunning()`. At 2000 sps, the deceleration adds ~130 steps to the measured width, causing it to exceed the 5% tolerance against the precise 3-touch reference.
- **Fix Strategy:** Capture `p2Pos` immediately when the `targetState` is detected in the while-loop, before calling `s->stopMove()`.

### [ID 2] ElegantOTA Reboot missing loop()
- **Symptom:** OTA updates reported "OK" but didn't reboot into new firmware.
- **Root Cause:** `ElegantOTA.loop()` was not called in `main_v4.cpp` loop. The reboot flag was set but never executed.
- **Fix:** Added `ElegantOTA.loop()` to `loop()`.

### [ID 3] NoiseEngine Redundancy
- **Issue:** `Synthesis.cpp` contains a full copy of the noise shaping math instead of using the `NoiseEngine` class from `NoiseEngine.h`.
- **Fix Strategy:** Instantiate `NoiseEngine` in `Synthesis` and delegate the sampling to `getVal()`.

### [ID 4] Tacho ISR Dead Code
- **Issue:** `Hal_Tacho.cpp` contains template ISRs and `attachInterrupt` attempts that were abandoned in favor of the 1kHz `tachoPollTask` (due to GPIO 15 strapping issues).
- **Action:** Remove dead ISR code for Bauhaus-strict cleanliness.

### [ID 5] Z-Motor Sweet-Spot vs Hard-Limit
- **Discovery:** Characterization found a Sweet-Spot at 1000mA for the Z-motor, but `Characterization.cpp` has a `MOTOR_CURRENT_HARD_MAX` of 900mA (Pancake datasheet).
- **Decision:** Stick to 900mA safety limit for production, but document that performance peaks slightly higher.
