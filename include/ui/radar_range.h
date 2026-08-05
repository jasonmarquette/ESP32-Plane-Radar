#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::radar {

struct RangePreset {
  float ring3_km;
  float outer_km;
};

constexpr float kRing3ToOuterKm = 4.0f / 3.0f;

constexpr RangePreset kRangePresets[] = {
    {5.0f, 5.0f * kRing3ToOuterKm},
    {10.0f, 10.0f * kRing3ToOuterKm},
    {15.0f, 15.0f * kRing3ToOuterKm},
    {25.0f, 25.0f * kRing3ToOuterKm},
};

constexpr size_t kRangePresetCount =
    sizeof(kRangePresets) / sizeof(kRangePresets[0]);

void rangeInit();
void rangeNext();
const RangePreset& rangeCurrent();
uint8_t rangeIndex();
float fetchRadiusKm();

/** Save a portal range value matching 5, 10, 15, or 25 km. */
bool saveRangeFromPortal(const char* ring3_km_value);

bool useMiles();
bool showRunways();
void saveMilesFromPortal(const char* checkbox_value);
void saveRunwaysFromPortal(const char* checkbox_value);
void formatRing3Label(char* buf, size_t len, float ring3_km, bool use_miles);
void formatCurrentRing3Label(char* buf, size_t len);
void unitsReset();

}  // namespace ui::radar
