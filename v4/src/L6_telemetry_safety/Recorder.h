#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// L6 — Phase 10 G: Performance-Recorder.
//
// Eigener Ringbuffer für Player-Snapshots (posDeg[4] + flightX/Y + Timestamp).
// Bewusst getrennt vom Engineering-`Telemetry`-Block, weil andere Frequenz,
// andere Felder, andere User-Erwartung (Start/Stop per Button, nicht per
// Test-Run-Reset).
//
// Kapazität: 600 Samples × 28 Byte ≈ 16.8 KB → ~60 s bei 10 Hz.

namespace Recorder {

constexpr size_t   CAPACITY     = 600;       // Sample-Slots im Ringbuffer
constexpr uint32_t INTERVAL_MS  = 100;       // 10 Hz Sampling

struct Sample {
    uint32_t t_ms;       // Millis seit Recording-Start
    float    posDeg[4];  // Live-Position der Motoren
    float    flightX;
    float    flightY;
};

void start();
void stop();
bool isRecording();
size_t count();          // wieviele Samples sind aktuell im Buffer
uint32_t startedAtMs();  // millis() bei start(), 0 wenn idle
// Throttled Sample-Aufnahme — wird aus Synthesis::tick() bei 100 Hz aufgerufen,
// schreibt nur alle INTERVAL_MS einen Sample. No-op wenn nicht recording.
void tick(float flightX, float flightY, const float posDeg[4]);

// CSV-Ausgabe, Reihenfolge älteste→neueste Sample.
String getCsv();

void registerHandlers(AsyncWebServer& server);

} // namespace Recorder
