#include "Storage_Calib.h"
#include <Preferences.h>
#include "../L0_platform/Logger.h"
#include "../L0_platform/Types.h"

namespace StorageCalib {

static constexpr const char* NS = "calib";
static constexpr uint32_t SCHEMA = 4004;

static String key(uint8_t motorIdx) {
    return String("m") + (char)('X' + motorIdx);
}

// Bug 49 (v4.4.12): RAM-Cache für Calib-Daten. /bounds-Handler rief load()
// 5x pro Request → 5 NVS-Flash-Reads à ~1 ms. Mit Cache: erster Aufruf füllt,
// danach reine RAM-Reads. Invalidation bei save() (write-through).
static v4::CalibrationData cache[4];
static bool                cacheFilled[4] = { false, false, false, false };

void load(uint8_t motorIdx, v4::CalibrationData& out) {
    if (motorIdx >= 4) return;
    if (cacheFilled[motorIdx]) {
        out = cache[motorIdx];
        return;
    }
    Preferences p;
    p.begin(NS, true);
    String k = key(motorIdx);
    if (p.getBytesLength(k.c_str()) == sizeof(v4::CalibrationData)) {
        p.getBytes(k.c_str(), &out, sizeof(v4::CalibrationData));
        if (out.nvsVersion != SCHEMA) {
            out.valid = false;
            Logger::addLog(String("CAL load M") + v4::motorName(motorIdx) + ": stale schema");
        }
    } else {
        out.valid = false;
    }
    p.end();
    cache[motorIdx]       = out;
    cacheFilled[motorIdx] = true;
}

void save(uint8_t motorIdx, const v4::CalibrationData& data) {
    if (motorIdx >= 4) return;
    Preferences p;
    p.begin(NS, false);
    v4::CalibrationData d = data;
    d.nvsVersion = SCHEMA;
    p.putBytes(key(motorIdx).c_str(), &d, sizeof(v4::CalibrationData));
    p.end();
    // Cache write-through: nach save() ist NVS und RAM konsistent.
    cache[motorIdx]       = d;
    cacheFilled[motorIdx] = true;
    Logger::addLog(String("CAL save M") + v4::motorName(motorIdx));
}

} // namespace StorageCalib
