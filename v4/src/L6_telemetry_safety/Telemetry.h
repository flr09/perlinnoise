#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// L6 — Telemetrie: 10Hz-Poll der TMC-Register auf Core 0, CSV-Ringbuffer.

namespace Telemetry {

constexpr size_t MAX_BYTES = 52000;
constexpr unsigned long INTERVAL_MS = 50;

void init();
void resetBuffer();
void recordDataPoint(uint8_t motorIdx, const char* phase, float val);
// recordEvent: wie recordDataPoint, aber OHNE 50ms-Throttle. Für seltene
// Marker (Cal-Phasen, Test-Stages), die nicht verloren gehen dürfen.
void recordEvent(uint8_t motorIdx, const char* phase, float val);
String getCsv();
void registerHandlers(AsyncWebServer& server);

uint16_t getLatestSg(uint8_t motorIdx);
uint8_t  getLatestCs(uint8_t motorIdx);
bool     isStalled(uint8_t motorIdx);

} // namespace Telemetry
