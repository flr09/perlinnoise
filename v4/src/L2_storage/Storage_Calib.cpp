#include "Storage_Calib.h"
#include <Preferences.h>
#include "../L0_platform/Logger.h"

namespace StorageCalib {

static constexpr const char* NS = "calib";
static constexpr uint32_t SCHEMA = 4002;

static String key(uint8_t motorIdx) {
    return String("m") + (char)('X' + motorIdx);
}

void load(uint8_t motorIdx, v4::CalibrationData& out) {
    if (motorIdx >= 4) return;
    Preferences p;
    p.begin(NS, true);
    String k = key(motorIdx);
    if (p.getBytesLength(k.c_str()) == sizeof(v4::CalibrationData)) {
        p.getBytes(k.c_str(), &out, sizeof(v4::CalibrationData));
        if (out.nvsVersion != SCHEMA) {
            out.valid = false;
            Logger::addLog(String("CAL load M") + (char)('X' + motorIdx) + ": stale schema");
        }
    } else {
        out.valid = false;
    }
    p.end();
}

void save(uint8_t motorIdx, const v4::CalibrationData& data) {
    if (motorIdx >= 4) return;
    Preferences p;
    p.begin(NS, false);
    v4::CalibrationData d = data;
    d.nvsVersion = SCHEMA;
    p.putBytes(key(motorIdx).c_str(), &d, sizeof(v4::CalibrationData));
    p.end();
    Logger::addLog(String("CAL save M") + (char)('X' + motorIdx));
}

} // namespace StorageCalib
