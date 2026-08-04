#include "services/map_background.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <TJpg_Decoder.h>
#include <WiFiClientSecure.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace services::map_background {
namespace {

constexpr char kPrefsNamespace[] = "map";
constexpr char kPrefsApiKey[] = "maptiler_key";
constexpr char kMapStyle[] = "dataviz-light";
constexpr int kMapSizePx = 320;
constexpr size_t kMaxApiKeyLen = 96;
constexpr size_t kMaxJpegBytes = 180 * 1024;

char s_api_key[kMaxApiKeyLen + 1] = {};
bool s_initialized = false;
bool s_invalidated = true;
LGFX_Sprite* s_decode_target = nullptr;

struct CacheKey {
  double lat = 0.0;
  double lon = 0.0;
  float outer_km = 0.0f;
  bool valid = false;
};

CacheKey s_cache;

bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h,
                uint16_t* bitmap) {
  if (s_decode_target == nullptr || y >= kMapSizePx || x >= kMapSizePx) {
    return false;
  }

  const uint16_t clipped_w =
      static_cast<uint16_t>(std::min<int>(w, kMapSizePx - x));
  const uint16_t clipped_h =
      static_cast<uint16_t>(std::min<int>(h, kMapSizePx - y));

  for (uint16_t row = 0; row < clipped_h; ++row) {
    s_decode_target->pushImage(x, y + row, clipped_w, 1,
                               bitmap + static_cast<size_t>(row) * w);
  }
  return true;
}

float zoomForRange(double lat, float outer_radius_km) {
  // MapTiler's tile pyramid uses 512 px tiles. Choose a fractional zoom that
  // makes the visible map width approximately the radar's outer diameter.
  constexpr double kMetersPerPixelAtEquatorZoom0 = 78271.516964;
  const double lat_rad = lat * 0.017453292519943295;
  const double ground_width_m =
      std::max(1000.0, static_cast<double>(outer_radius_km) * 2000.0);
  const double numerator =
      kMapSizePx * kMetersPerPixelAtEquatorZoom0 * std::cos(lat_rad);
  const double zoom = std::log2(numerator / ground_width_m);
  return static_cast<float>(std::max(1.0, std::min(18.0, zoom)));
}

String staticMapUrl(double lat, double lon, float outer_radius_km) {
  const float zoom = zoomForRange(lat, outer_radius_km);
  String url;
  url.reserve(240);
  url += "https://api.maptiler.com/maps/";
  url += kMapStyle;
  url += "/static/";
  url += String(lon, 6);
  url += ',';
  url += String(lat, 6);
  url += ',';
  url += String(zoom, 2);
  url += '/';
  url += String(kMapSizePx);
  url += 'x';
  url += String(kMapSizePx);
  url += ".jpg?key=";
  url += s_api_key;
  return url;
}

bool cacheMatches(double lat, double lon, float outer_radius_km) {
  if (!s_cache.valid || s_invalidated) {
    return false;
  }
  return std::fabs(s_cache.lat - lat) < 0.000001 &&
         std::fabs(s_cache.lon - lon) < 0.000001 &&
         std::fabs(s_cache.outer_km - outer_radius_km) < 0.01f;
}

void updateCacheKey(double lat, double lon, float outer_radius_km) {
  s_cache.lat = lat;
  s_cache.lon = lon;
  s_cache.outer_km = outer_radius_km;
  s_cache.valid = true;
  s_invalidated = false;
}

}  // namespace

void init() {
  if (s_initialized) {
    return;
  }

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    const String saved = prefs.getString(kPrefsApiKey, "");
    saved.substring(0, kMaxApiKeyLen).toCharArray(s_api_key,
                                                  sizeof(s_api_key));
    prefs.end();
  }
  s_initialized = true;
}

bool configured() {
  init();
  return s_api_key[0] != '\0';
}

const char* apiKey() {
  init();
  return s_api_key;
}

void saveApiKey(const char* key) {
  init();
  const char* value = key == nullptr ? "" : key;
  std::strncpy(s_api_key, value, kMaxApiKeyLen);
  s_api_key[kMaxApiKeyLen] = '\0';

  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    if (s_api_key[0] == '\0') {
      prefs.remove(kPrefsApiKey);
    } else {
      prefs.putString(kPrefsApiKey, s_api_key);
    }
    prefs.end();
  }
  invalidate();
}

void clear() { saveApiKey(""); }

void invalidate() {
  s_invalidated = true;
  s_cache.valid = false;
}

bool draw(LGFX_Sprite& target, double lat, double lon, float outer_radius_km) {
  init();
  if (!configured() || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  // The caller normally redraws the frame sprite every aircraft refresh. A
  // later cache layer will preserve decoded pixels; for now avoid a duplicate
  // request only within an unchanged draw cycle.
  if (cacheMatches(lat, lon, outer_radius_km)) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  const String url = staticMapUrl(lat, lon, outer_radius_km);
  if (!http.begin(client, url)) {
    Serial.println("map: HTTP begin failed");
    return false;
  }
  http.setTimeout(12000);

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("map: HTTP %d\n", status);
    http.end();
    return false;
  }

  const int content_length = http.getSize();
  if (content_length <= 0 ||
      static_cast<size_t>(content_length) > kMaxJpegBytes) {
    Serial.printf("map: invalid JPEG size %d\n", content_length);
    http.end();
    return false;
  }

  uint8_t* jpeg = static_cast<uint8_t*>(std::malloc(content_length));
  if (jpeg == nullptr) {
    Serial.printf("map: unable to allocate %d bytes\n", content_length);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t bytes_read =
      stream->readBytes(jpeg, static_cast<size_t>(content_length));
  http.end();

  if (bytes_read != static_cast<size_t>(content_length)) {
    Serial.printf("map: short read %u/%d\n", static_cast<unsigned>(bytes_read),
                  content_length);
    std::free(jpeg);
    return false;
  }

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(jpegOutput);
  s_decode_target = &target;
  const JRESULT result = TJpgDec.drawJpg(0, 0, jpeg, bytes_read);
  s_decode_target = nullptr;
  std::free(jpeg);

  if (result != JDR_OK) {
    Serial.printf("map: JPEG decode failed (%d)\n", result);
    return false;
  }

  updateCacheKey(lat, lon, outer_radius_km);
  Serial.printf("map: rendered %.6f,%.6f range %.1f km\n", lat, lon,
                outer_radius_km);
  return true;
}

}  // namespace services::map_background
