#pragma once

namespace services::location {

/** Load saved station settings from NVS, or use config defaults. */
void init();

double lat();
double lon();

/** Optional station/airport ICAO label, such as KIAH. */
const char* icao();

/** ADS-B search-radius override in km. Zero means follow the display range. */
float adsbRadiusKm();

/** Parse portal strings, validate, persist to NVS, and update runtime values. */
bool saveFromStrings(const char* lat_str, const char* lon_str);
bool saveSettingsFromStrings(const char* lat_str, const char* lon_str,
                             const char* icao_str,
                             const char* adsb_radius_str);

/** Clear stored station settings. */
void clear();

}  // namespace services::location
