#pragma once
#include <Arduino.h>

// L6 — Motorprofil (Lookup-Tabelle aus SpeedTest-Daten)
//
// Pro Motor ein Profil mit RPM-Stützpunkten. Watchdog interpoliert daraus
// das Antizipationsfenster. Aktuell nur für Motor 0/Z aktiv genutzt — Multi-Motor
// kommt in L6_v2 wenn alle 4 Motoren physisch montiert sind.

namespace MotorProfileNs {

constexpr uint8_t  BINS    = 20;
constexpr uint32_t NVS_VER = 4002U;

struct ProfilePoint {
    float    rpm;
    float    periodMeanUs;
    float    periodSigmaUs;
    uint16_t sgMean;
    uint8_t  csActual;
};

struct Profile {
    ProfilePoint points[BINS];
    uint8_t      count = 0;
    uint32_t     nvsVersion = NVS_VER;
    bool         valid = false;
};

extern Profile profiles[4];

void clearProfile(uint8_t motorIdx);
bool addPoint(uint8_t motorIdx, float rpm, float periodMean, float periodSigma,
              uint16_t sgMean, uint8_t csActual);
bool getExpected(uint8_t motorIdx, float rpm, float* outMean, float* outSigma);

void saveProfile(uint8_t motorIdx);
void loadProfile(uint8_t motorIdx);
void loadAll();

} // namespace MotorProfileNs
