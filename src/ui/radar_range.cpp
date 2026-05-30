#include "ui/radar_range.h"

#include <Preferences.h>

namespace ui::radar {

namespace {

constexpr char kPrefsNamespace[] = "roundscreen";
constexpr char kPrefsRangeKey[] = "rangeIdx";
constexpr uint8_t kDefaultRangeIndex = 1;  // 10 km

Preferences s_prefs;
uint8_t s_range_index = kDefaultRangeIndex;

void saveRangeIndex() {
  s_prefs.putUChar(kPrefsRangeKey, s_range_index);
}

}  // namespace

void rangeInit() {
  s_prefs.begin(kPrefsNamespace, false);
  const uint8_t saved = s_prefs.getUChar(kPrefsRangeKey, kDefaultRangeIndex);
  s_range_index =
      (saved < kRangePresetCount) ? saved : kDefaultRangeIndex;
}

void rangeNext() {
  s_range_index = static_cast<uint8_t>((s_range_index + 1) % kRangePresetCount);
  saveRangeIndex();
}

const RangePreset& rangeCurrent() { return kRangePresets[s_range_index]; }

uint8_t rangeIndex() { return s_range_index; }

}  // namespace ui::radar
