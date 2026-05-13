#include "Storage_Runtime.h"
#include <Preferences.h>
#include "../L0_platform/Logger.h"

namespace StorageRuntime {

static constexpr const char* NS = "rtconf";
// v4201 (Phase 10 A, 2026-05-12): + Point offsets[4] für räumliches Modell.
// v4202 (Phase 10 C, 2026-05-12): + float posDeg[4] für Coordinate-Mode (moveType=8).
// Alte Blobs werden in load() jeweils per Größen- + Schema-Mismatch verworfen.
static constexpr uint32_t SCHEMA = 4202;
static constexpr uint32_t DEBOUNCE_MS = 5000;

// NVS-Blob: explizites Layout, damit Schema-Bumps kontrollierbar sind und
// nicht jedes Aliasing-Detail der RuntimeConfig-Struct die NVS-Größe ändert.
struct Blob {
    uint32_t schema = SCHEMA;
    uint8_t  moveType;
    float    speed;
    float    angle;
    float    radius;
    float    framesize;
    float    contrast;
    float    zShape;
    float    edgeC;
    float    rangeDeg;
    float    mspace;
    uint8_t  dynamics;
    uint8_t  fan;
    uint8_t  lamp;
    float    stepAngle;
    float    stepOffset;
    float    holdMs;
    uint32_t accelMax;
    float    offX[4];   // v4201: räumliche Motor-Positionen, Noise-Raum-Einheiten
    float    offY[4];
    float    posDeg[4]; // v4202: Coordinate-Mode-Ziel pro Motor [°]
    uint8_t  _pad[2] = {0, 0};
};

static unsigned long lastTouchMs = 0;
static bool          dirty       = false;

void load(v4::RuntimeConfig& cfg) {
    Preferences p;
    p.begin(NS, true);
    Blob b;
    if (p.getBytesLength("last") == sizeof(Blob)) {
        p.getBytes("last", &b, sizeof(Blob));
        if (b.schema == SCHEMA) {
            cfg.moveType   = b.moveType;
            cfg.speed      = b.speed;
            cfg.angle      = b.angle;
            cfg.radius     = b.radius;
            cfg.framesize  = b.framesize;
            cfg.contrast   = b.contrast;
            cfg.zShape     = b.zShape;
            cfg.edgeC      = b.edgeC;
            cfg.rangeDeg   = b.rangeDeg;
            cfg.mspace     = b.mspace;
            cfg.dynamics   = b.dynamics;
            cfg.fan        = b.fan;
            cfg.lamp       = b.lamp;
            cfg.stepAngle  = b.stepAngle;
            cfg.stepOffset = b.stepOffset;
            cfg.holdMs     = b.holdMs;
            cfg.accelMax   = b.accelMax;
            for (uint8_t i = 0; i < 4; i++) {
                cfg.offsets[i].x = b.offX[i];
                cfg.offsets[i].y = b.offY[i];
                cfg.posDeg[i]    = b.posDeg[i];
            }
            Logger::addLog("RT: loaded NVS config");
        } else {
            Logger::addLog(String("RT: NVS schema stale (")
                + b.schema + " != " + SCHEMA + "), defaults aktiv");
        }
    } else {
        Logger::addLog("RT: NVS leer, defaults aktiv");
    }
    p.end();
}

void touch() {
    lastTouchMs = millis();
    dirty       = true;
}

static void writeNow(const v4::RuntimeConfig& cfg) {
    Preferences p;
    p.begin(NS, false);
    Blob b;
    b.moveType   = (uint8_t)cfg.moveType;
    b.speed      = cfg.speed;
    b.angle      = cfg.angle;
    b.radius     = cfg.radius;
    b.framesize  = cfg.framesize;
    b.contrast   = cfg.contrast;
    b.zShape     = cfg.zShape;
    b.edgeC      = cfg.edgeC;
    b.rangeDeg   = cfg.rangeDeg;
    b.mspace     = cfg.mspace;
    b.dynamics   = (uint8_t)cfg.dynamics;
    b.fan        = (uint8_t)cfg.fan;
    b.lamp       = (uint8_t)cfg.lamp;
    b.stepAngle  = cfg.stepAngle;
    b.stepOffset = cfg.stepOffset;
    b.holdMs     = cfg.holdMs;
    b.accelMax   = cfg.accelMax;
    for (uint8_t i = 0; i < 4; i++) {
        b.offX[i]   = cfg.offsets[i].x;
        b.offY[i]   = cfg.offsets[i].y;
        b.posDeg[i] = cfg.posDeg[i];
    }
    p.putBytes("last", &b, sizeof(Blob));
    p.end();
    dirty = false;
    Logger::addLog("RT: saved");
}

void tickFlush(const v4::RuntimeConfig& cfg) {
    if (!dirty) return;
    if (millis() - lastTouchMs < DEBOUNCE_MS) return;
    writeNow(cfg);
}

void forceFlush(const v4::RuntimeConfig& cfg) {
    if (dirty) writeNow(cfg);
}

// --- Presets (Bug-ID 23b) ---
//
// Eigener NVS-Namespace `presets`, Slot-Keys "s0"..."s7". Wir verwenden
// dasselbe Blob-Layout wie für die Live-Config + ein `valid`-Flag. Save/Load
// sofort — keine Debounce, weil Preset-Aktionen explizite User-Klicks sind
// (selten, kein Wear-Out-Risiko).

static constexpr const char* NS_PR = "presets";

struct PresetBlob {
    uint32_t schema = SCHEMA;
    bool     valid  = false;
    uint8_t  _pad0[3] = {0,0,0};
    uint8_t  moveType;
    float    speed;
    float    angle;
    float    radius;
    float    framesize;
    float    contrast;
    float    zShape;
    float    edgeC;
    float    rangeDeg;
    float    mspace;
    uint8_t  dynamics;
    uint8_t  fan;
    uint8_t  lamp;
    float    stepAngle;
    float    stepOffset;
    float    holdMs;
    uint32_t accelMax;
    float    offX[4];   // v4201: Coordinate-Snapshot inkl. Spatial-Modell
    float    offY[4];
    float    posDeg[4]; // v4202: Coordinate-Mode-Ziele pro Motor
    uint8_t  _pad1[1] = {0};
};

static String pKey(uint8_t slot) { return String("s") + slot; }

void savePreset(uint8_t slot, const v4::RuntimeConfig& cfg) {
    if (slot >= 8) return;
    Preferences p; p.begin(NS_PR, false);
    PresetBlob b;
    b.valid      = true;
    b.moveType   = (uint8_t)cfg.moveType;
    b.speed      = cfg.speed;
    b.angle      = cfg.angle;
    b.radius     = cfg.radius;
    b.framesize  = cfg.framesize;
    b.contrast   = cfg.contrast;
    b.zShape     = cfg.zShape;
    b.edgeC      = cfg.edgeC;
    b.rangeDeg   = cfg.rangeDeg;
    b.mspace     = cfg.mspace;
    b.dynamics   = (uint8_t)cfg.dynamics;
    b.fan        = (uint8_t)cfg.fan;
    b.lamp       = (uint8_t)cfg.lamp;
    b.stepAngle  = cfg.stepAngle;
    b.stepOffset = cfg.stepOffset;
    b.holdMs     = cfg.holdMs;
    b.accelMax   = cfg.accelMax;
    for (uint8_t i = 0; i < 4; i++) {
        b.offX[i]   = cfg.offsets[i].x;
        b.offY[i]   = cfg.offsets[i].y;
        b.posDeg[i] = cfg.posDeg[i];
    }
    p.putBytes(pKey(slot).c_str(), &b, sizeof(PresetBlob));
    p.end();
    Logger::addLog(String("PRESET ") + slot + ": saved");
}

bool loadPreset(uint8_t slot, v4::RuntimeConfig& cfg) {
    if (slot >= 8) return false;
    Preferences p; p.begin(NS_PR, true);
    PresetBlob b;
    bool ok = false;
    if (p.getBytesLength(pKey(slot).c_str()) == sizeof(PresetBlob)) {
        p.getBytes(pKey(slot).c_str(), &b, sizeof(PresetBlob));
        if (b.valid && b.schema == SCHEMA) {
            cfg.moveType   = b.moveType;
            cfg.speed      = b.speed;
            cfg.angle      = b.angle;
            cfg.radius     = b.radius;
            cfg.framesize  = b.framesize;
            cfg.contrast   = b.contrast;
            cfg.zShape     = b.zShape;
            cfg.edgeC      = b.edgeC;
            cfg.rangeDeg   = b.rangeDeg;
            cfg.mspace     = b.mspace;
            cfg.dynamics   = b.dynamics;
            cfg.fan        = b.fan;
            cfg.lamp       = b.lamp;
            cfg.stepAngle  = b.stepAngle;
            cfg.stepOffset = b.stepOffset;
            cfg.holdMs     = b.holdMs;
            cfg.accelMax   = b.accelMax;
            for (uint8_t i = 0; i < 4; i++) {
                cfg.offsets[i].x = b.offX[i];
                cfg.offsets[i].y = b.offY[i];
                cfg.posDeg[i]    = b.posDeg[i];
            }
            // Live-Wert geändert → in normaler Debounce-Save mitnehmen
            touch();
            ok = true;
            Logger::addLog(String("PRESET ") + slot + ": loaded");
        }
    }
    p.end();
    return ok;
}

bool isPresetValid(uint8_t slot) {
    if (slot >= 8) return false;
    Preferences p; p.begin(NS_PR, true);
    PresetBlob b;
    bool ok = false;
    if (p.getBytesLength(pKey(slot).c_str()) == sizeof(PresetBlob)) {
        p.getBytes(pKey(slot).c_str(), &b, sizeof(PresetBlob));
        ok = (b.valid && b.schema == SCHEMA);
    }
    p.end();
    return ok;
}

} // namespace StorageRuntime
