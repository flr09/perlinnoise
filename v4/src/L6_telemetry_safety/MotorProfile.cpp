#include "MotorProfile.h"
#include <Preferences.h>
#include "../L0_platform/Logger.h"
#include <string.h>

namespace MotorProfileNs {

Profile profiles[4];

static String key(uint8_t motorIdx) {
    return String("p") + (char)('X' + motorIdx);
}

void clearProfile(uint8_t i) {
    if (i >= 4) return;
    profiles[i].count = 0;
    profiles[i].valid = false;
    profiles[i].nvsVersion = NVS_VER;
    memset(profiles[i].points, 0, sizeof(profiles[i].points));
}

bool addPoint(uint8_t i, float rpm, float pMean, float pSigma,
              uint16_t sgMean, uint8_t cs) {
    if (i >= 4) return false;
    auto& p = profiles[i];
    if (p.count >= BINS) return false;
    uint8_t pos = p.count;
    for (uint8_t k = 0; k < p.count; k++) if (rpm < p.points[k].rpm) { pos = k; break; }
    for (uint8_t k = p.count; k > pos; k--) p.points[k] = p.points[k-1];
    p.points[pos] = { rpm, pMean, pSigma, sgMean, cs };
    p.count++;
    p.valid = (p.count >= 3);
    return true;
}

bool getExpected(uint8_t i, float rpm, float* outMean, float* outSigma) {
    if (i >= 4) return false;
    auto& p = profiles[i];
    if (!p.valid || p.count < 2) return false;
    if (rpm < p.points[0].rpm) return false;
    if (rpm >= p.points[p.count - 1].rpm) {
        *outMean  = p.points[p.count-1].periodMean;
        *outSigma = p.points[p.count-1].periodSigma;
        return true;
    }
    for (uint8_t k = 0; k < p.count - 1; k++) {
        const auto& lo = p.points[k];
        const auto& hi = p.points[k + 1];
        if (rpm >= lo.rpm && rpm < hi.rpm) {
            float t = (rpm - lo.rpm) / (hi.rpm - lo.rpm);
            *outMean  = lo.periodMean  + t * (hi.periodMean  - lo.periodMean);
            *outSigma = lo.periodSigma + t * (hi.periodSigma - lo.periodSigma);
            if (*outSigma < 1.0f) *outSigma = 1.0f;
            return true;
        }
    }
    return false;
}

void saveProfile(uint8_t i) {
    if (i >= 4) return;
    profiles[i].nvsVersion = NVS_VER;
    Preferences prefs;
    prefs.begin("profile", false);
    prefs.putBytes(key(i).c_str(), &profiles[i], sizeof(Profile));
    prefs.end();
}

void loadProfile(uint8_t i) {
    if (i >= 4) return;
    Preferences prefs;
    prefs.begin("profile", true);
    String k = key(i);
    if (prefs.getBytesLength(k.c_str()) == sizeof(Profile)) {
        Profile tmp;
        prefs.getBytes(k.c_str(), &tmp, sizeof(Profile));
        if (tmp.nvsVersion == NVS_VER && tmp.count <= BINS) {
            profiles[i] = tmp;
        } else clearProfile(i);
    } else clearProfile(i);
    prefs.end();
}

void loadAll() {
    for (uint8_t i = 0; i < 4; i++) loadProfile(i);
}

} // namespace MotorProfileNs
