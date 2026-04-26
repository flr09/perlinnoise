#pragma once
#include <Arduino.h>
#include "Config.h"

// --- Motorprofil: Lookup-Tabelle aus SpeedTest-Daten ---
// Wird einmalig beim SpeedTest befüllt und im NVS gespeichert.
// Watchdog nutzt sie zur Interpolation des Antizipationsfensters.

#define PROFILE_BINS    20       // Max RPM-Stützstellen
#define PROFILE_NVS_VER 4001U   // Strukturversion — bei Änderung NVS invalidiert

struct ProfilePoint {
    float    rpm;           // Ziel-RPM dieser Stufe
    float    periodMean;    // Erwartete Tacho-Periode (ms), Mittelwert
    float    periodSigma;   // Standardabweichung der Periode (ms)
    uint16_t sgMean;        // Mittlerer SG_RESULT bei dieser Drehzahl
    uint8_t  csActual;      // cs_actual (Proxy für tatsächlichen Strom)
};

struct MotorProfile {
    ProfilePoint points[PROFILE_BINS];
    uint8_t      count;
    uint32_t     nvsVersion;
    bool         valid;
};

extern MotorProfile motorProfile;

// Profil zurücksetzen (vor neuem SpeedTest)
void profileClear();

// Stützpunkt hinzufügen — gibt false zurück wenn Tabelle voll
bool profileAddPoint(float rpm, float periodMean, float periodSigma,
                     uint16_t sgMean, uint8_t csActual);

// Lineare Interpolation: erwartetePeriode + Sigma für gegebene RPM
// Gibt false zurück wenn Profil nicht valide oder RPM außerhalb Bereich
bool profileGetExpected(float rpm, float* outMean, float* outSigma);

// NVS-Persistenz
void profileSave();
void profileLoad();
