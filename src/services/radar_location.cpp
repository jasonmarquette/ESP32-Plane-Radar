#include "services/radar_location.h"

#include <Preferences.h>
#include <cstdlib>
#include <cstring>
#include <cctype>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";
constexpr char kKeyIcao[] = "icao";
constexpr char kKeyAdsbRadius[] = "adsbRadius";
constexpr size_t kIcaoMaxLen = 7;

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;
char s_icao[kIcaoMaxLen + 1] = {};
float s_adsb_radius_km = 0.0f;

bool parseDouble(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') return false;
  char* end = nullptr;
  const double value = strtod(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) return false;
  *out = value;
  return true;
}

bool parseOptionalFloat(const char* text, float* out) {
  if (text == nullptr || text[0] == '\0') {
    *out = 0.0f;
    return true;
  }
  char* end = nullptr;
  const float value = strtof(text, &end);
  if (end == text || (end != nullptr && *end != '\0')) return false;
  *out = value;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void normalizeIcao(const char* input, char* output, size_t output_len) {
  if (output_len == 0) return;
  size_t written = 0;
  if (input != nullptr) {
    while (*input != '\0' && written + 1 < output_len) {
      const unsigned char c = static_cast<unsigned char>(*input++);
      if (std::isalnum(c)) output[written++] = static_cast<char>(std::toupper(c));
    }
  }
  output[written] = '\0';
}

void persist() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return;
  prefs.putDouble(kKeyLat, s_lat);
  prefs.putDouble(kKeyLon, s_lon);
  if (s_icao[0] == '\0') prefs.remove(kKeyIcao);
  else prefs.putString(kKeyIcao, s_icao);
  if (s_adsb_radius_km <= 0.0f) prefs.remove(kKeyAdsbRadius);
  else prefs.putFloat(kKeyAdsbRadius, s_adsb_radius_km);
  prefs.end();
}

}  // namespace

void init() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) return;

  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double saved_lat = prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double saved_lon = prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(saved_lat, saved_lon)) {
      s_lat = saved_lat;
      s_lon = saved_lon;
    }
  }

  const String saved_icao = prefs.getString(kKeyIcao, "");
  normalizeIcao(saved_icao.c_str(), s_icao, sizeof(s_icao));

  const float saved_radius = prefs.getFloat(kKeyAdsbRadius, 0.0f);
  s_adsb_radius_km = (saved_radius >= 1.0f && saved_radius <= 500.0f)
                         ? saved_radius
                         : 0.0f;
  prefs.end();
}

double lat() { return s_lat; }
double lon() { return s_lon; }
const char* icao() { return s_icao; }
float adsbRadiusKm() { return s_adsb_radius_km; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  return saveSettingsFromStrings(lat_str, lon_str, s_icao,
                                 s_adsb_radius_km > 0.0f
                                     ? String(s_adsb_radius_km, 1).c_str()
                                     : "");
}

bool saveSettingsFromStrings(const char* lat_str, const char* lon_str,
                             const char* icao_str,
                             const char* adsb_radius_str) {
  double new_lat = 0.0;
  double new_lon = 0.0;
  float new_radius = 0.0f;
  if (!parseDouble(lat_str, &new_lat) || !parseDouble(lon_str, &new_lon) ||
      !validLatLon(new_lat, new_lon) ||
      !parseOptionalFloat(adsb_radius_str, &new_radius) ||
      (new_radius != 0.0f && (new_radius < 1.0f || new_radius > 500.0f))) {
    return false;
  }

  s_lat = new_lat;
  s_lon = new_lon;
  normalizeIcao(icao_str, s_icao, sizeof(s_icao));
  s_adsb_radius_km = new_radius;
  persist();

  Serial.printf("Radar station saved: %s %.6f, %.6f; ADS-B radius %s\n",
                s_icao[0] != '\0' ? s_icao : "(unlabeled)", s_lat, s_lon,
                s_adsb_radius_km > 0.0f
                    ? String(s_adsb_radius_km, 1).c_str()
                    : "auto");
  return true;
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
  s_icao[0] = '\0';
  s_adsb_radius_km = 0.0f;
}

}  // namespace services::location
