#pragma once
#include <Arduino.h>

// L5a — Charakterisierungs-Programme (Engineering-Tests aus v3 + Field-Weakening-Test)

namespace Characterization {

void runSgLearn(uint8_t motorIdx);
void runSpeedTest(uint8_t motorIdx);
void runInertiaTest(uint8_t motorIdx);
void runCoastTest(uint8_t motorIdx);
void runKatapult(uint8_t motorIdx);
void runProfileTest(uint8_t motorIdx);
void runFreqSweep(uint8_t motorIdx);
void runTachoCutoffDiagnostic(uint8_t motorIdx);
void runSilentProfileTest(uint8_t motorIdx);
void runPerformanceShow(uint8_t motorIdx);
void runCurrentSweepHiRPM(uint8_t motorIdx);

} // namespace Characterization
