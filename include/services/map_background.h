#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace services::map_background {

void init();
bool configured();
const char* apiKey();
void saveApiKey(const char* key);
void clear();
void invalidate();

/** Draw the map into the normal 320x320 off-screen radar sprite. */
bool draw(LGFX_Sprite& target, double lat, double lon, float outer_radius_km);

/**
 * Draw the map directly into the LCD's upper-left 320x320 radar area.
 * This path uses no full-frame sprite and is intended for low-memory fallback.
 */
bool drawDirect(lgfx::LovyanGFX& target, double lat, double lon,
                float outer_radius_km);

}  // namespace services::map_background
