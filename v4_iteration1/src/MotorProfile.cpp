#include "MotorProfile.h"
#include <Preferences.h>

MotorProfile motorProfile;

static Preferences profilePrefs;

void profileClear() {
    motorProfile.count      = 0;
    motorProfile.valid      = false;
    motorProfile.nvsVersion = PROFILE_NVS_VER;
    memset(motorProfile.points, 0, sizeof(motorProfile.points));
}

bool profileAddPoint(float rpm, float periodMean, float periodSigma,
                     uint16_t sgMean, uint8_t csActual) {
    if (motorProfile.count >= PROFILE_BINS) return false;

    // Stützpunkt nach RPM einsortieren (aufsteigend)
    uint8_t pos = motorProfile.count;
    for (uint8_t i = 0; i < motorProfile.count; i++) {
        if (rpm < motorProfile.points[i].rpm) { pos = i; break; }
    }
    // Einträge nach rechts schieben
    for (uint8_t i = motorProfile.count; i > pos; i--) {
        motorProfile.points[i] = motorProfile.points[i - 1];
    }
    motorProfile.points[pos] = { rpm, periodMean, periodSigma, sgMean, csActual };
    motorProfile.count++;
    motorProfile.valid = (motorProfile.count >= 3); // Mind. 3 Punkte für sinnvolle Interpolation
    return true;
}

bool profileGetExpected(float rpm, float* outMean, float* outSigma) {
    if (!motorProfile.valid || motorProfile.count < 2) return false;

    // Unterhalb des ersten Punkts: kein Watchdog-Fenster
    if (rpm < motorProfile.points[0].rpm) return false;
    // Oberhalb des letzten Punkts: letzten Punkt nutzen (kein Extrapolieren)
    if (rpm >= motorProfile.points[motorProfile.count - 1].rpm) {
        *outMean  = motorProfile.points[motorProfile.count - 1].periodMean;
        *outSigma = motorProfile.points[motorProfile.count - 1].periodSigma;
        return true;
    }

    // Lineare Interpolation zwischen den zwei umgebenden Punkten
    for (uint8_t i = 0; i < motorProfile.count - 1; i++) {
        const ProfilePoint& lo = motorProfile.points[i];
        const ProfilePoint& hi = motorProfile.points[i + 1];
        if (rpm >= lo.rpm && rpm < hi.rpm) {
            float t = (rpm - lo.rpm) / (hi.rpm - lo.rpm); // 0..1
            *outMean  = lo.periodMean  + t * (hi.periodMean  - lo.periodMean);
            *outSigma = lo.periodSigma + t * (hi.periodSigma - lo.periodSigma);
            // Sigma-Untergrenze: mindestens 1ms um Fehlauslösungen zu vermeiden
            if (*outSigma < 1.0f) *outSigma = 1.0f;
            return true;
        }
    }
    return false;
}

void profileSave() {
    motorProfile.nvsVersion = PROFILE_NVS_VER;
    profilePrefs.begin("profile", false);
    profilePrefs.putBytes("data", &motorProfile, sizeof(motorProfile));
    profilePrefs.end();
}

void profileLoad() {
    profilePrefs.begin("profile", true);
    size_t len = profilePrefs.getBytesLength("data");
    if (len == sizeof(MotorProfile)) {
        MotorProfile tmp;
        profilePrefs.getBytes("data", &tmp, sizeof(tmp));
        if (tmp.nvsVersion == PROFILE_NVS_VER && tmp.count <= PROFILE_BINS) {
            motorProfile = tmp;
        } else {
            profileClear(); // Veraltete oder korrupte Daten
        }
    } else {
        profileClear();
    }
    profilePrefs.end();
}
