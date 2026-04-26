#pragma once
#include "../L0_platform/Types.h"

// L2 — Persistierung der Kalibrierungsdaten pro Motor.
//
// Schema-Version 4001 (war 3619 in v3). Bei Versionswechsel werden alte
// Einträge invalidiert (`valid = false`).

namespace StorageCalib {

void load(uint8_t motorIdx, v4::CalibrationData& out);
void save(uint8_t motorIdx, const v4::CalibrationData& data);

} // namespace StorageCalib
