#pragma once

#include <algorithm>

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <WiFi.h>

namespace services::map_background {

/** Load the saved MapTiler API key from NVS. */
void init();

/** True when a non-empty API key is configured. */
bool configured();

/** Current key for setup-portal field prefilling. */
const char* apiKey();

/** Save a MapTiler API key from the setup portal. Empty disables maps. */
void saveApiKey(const char* key);

/** Clear the saved key and cached map state. */
void clear();

/** Force the next draw to download a fresh map. */
void invalidate();

/**
 * Download and draw a light static map into the supplied 320x320 sprite.
 * Returns false without modifying the sprite when maps are disabled or the
 * request/decoder fails, allowing the normal solid radar background fallback.
 */
bool draw(LGFX_Sprite& target, double lat, double lon, float outer_radius_km);

}  // namespace services::map_background
