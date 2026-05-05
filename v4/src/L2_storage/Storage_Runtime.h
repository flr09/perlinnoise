#pragma once
#include "../L5_programs/synthesis/RuntimeConfig.h"

// L2 — Persistierung der RuntimeConfig (Slider-Werte) in NVS.
//
// Bug-ID 23a: Slider-Werte (Speed, Contrast, ...) gingen bisher nach Reboot
// verloren. NVS-Schreiben bei JEDEM /set wäre der naheliegende Ansatz, würde
// aber zu NVS-Wear-Out führen (ESP32 ~100 k Cycles/Cell, Slider-Drag erzeugt
// 50+ Writes/s). Stattdessen: Debounce-Schema — `touch()` markiert nur
// dirty + Zeitstempel, `tickFlush()` aus dem main-loop schreibt erst nach
// DEBOUNCE_MS ohne weitere touch()-Calls.

namespace StorageRuntime {

// Beim Boot aufrufen, lädt letzten Stand in cfg. Bei fehlender oder
// schema-stale NVS-Section bleiben die Defaults aus RuntimeConfig.h.
void load(v4::RuntimeConfig& cfg);

// Markiert Config als geändert. Aus /set-Handler aufrufen.
// Setzt internen Zeitstempel — der eigentliche NVS-Write erfolgt erst
// in tickFlush() nach DEBOUNCE_MS Ruhe.
void touch();

// Periodisch aus dem main-loop aufrufen (z.B. alle 100 ms — Auflösung
// reicht für 5 s Debounce). Schreibt NVS wenn dirty + DEBOUNCE_MS Ruhe.
void tickFlush(const v4::RuntimeConfig& cfg);

// Erzwingt sofortiges Speichern (für Reboot-Pfade). Idempotent — wenn
// nichts dirty, kein Schreiben.
void forceFlush(const v4::RuntimeConfig& cfg);

// --- Presets (8 Slots, NVS-persistent) — Bug-ID 23b ---
// Slot 0..7. savePreset/loadPreset schreiben/lesen sofort (kein Debounce —
// User-Aktion ist explizit). isPresetValid testet ob Slot belegt ist.

void savePreset(uint8_t slot, const v4::RuntimeConfig& cfg);
bool loadPreset(uint8_t slot, v4::RuntimeConfig& cfg);  // true bei Erfolg
bool isPresetValid(uint8_t slot);

} // namespace StorageRuntime
