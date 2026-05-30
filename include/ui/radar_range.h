#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

/**
 * Range presets (label on ring 3 = ¾ of outer radius).
 *
 * Recommended for future ADS-B / local traffic on a 1.28″ display:
 *   5 km  — pattern / very local (airfield vicinity)
 *  10 km  — default; neighborhood spotting
 *  25 km  — metro / terminal-area picture
 *  50 km  — regional; fewer icons, wider context
 *
 * Outer radius (for aircraft math) is ring-3 distance ÷ 0.75.
 */
struct RangePreset {
  const char* ring3_label;
  float outer_km;
};

constexpr RangePreset kRangePresets[] = {
    {"5km", 6.7f},
    {"10km", 13.3f},
    {"25km", 33.3f},
    {"50km", 66.7f},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

/** Load saved range from flash (or default). Call once after boot. */
void rangeInit();
/** Cycle preset and save to flash. */
void rangeNext();
const RangePreset& rangeCurrent();
uint8_t rangeIndex();

}  // namespace ui::radar
