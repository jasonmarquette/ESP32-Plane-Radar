#include "services/map_background.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <PNGdec.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace services::map_background {
namespace {

constexpr char kPrefsNamespace[] = "map";
constexpr char kPrefsApiKey[] = "maptiler_key";
constexpr char kMapStyle[] = "dataviz-light";
constexpr char kTilePrefix[] = "/radar-tile-";
constexpr int kMapSizePx = 320;
constexpr int kTileSizePx = 256;
constexpr size_t kMaxApiKeyLen = 96;
constexpr size_t kMaxTileBytes = 300 * 1024;
constexpr unsigned long kRetryDelayMs = 60000UL;
constexpr uint8_t kPngSignature[8] = {0x89, 0x50, 0x4e, 0x47,
                                      0x0d, 0x0a, 0x1a, 0x0a};

char s_api_key[kMaxApiKeyLen + 1] = {};
bool s_initialized = false;
bool s_fs_ready = false;
bool s_invalidated = true;
unsigned long s_next_retry_ms = 0;

PNG s_png;
File s_png_file;
lgfx::LovyanGFX* s_decode_target = nullptr;
int s_decode_origin_x = 0;
int s_decode_origin_y = 0;
uint16_t s_png_line[kTileSizePx];

struct TileView {
  int zoom = 0;
  int min_x = 0;
  int max_x = 0;
  int min_y = 0;
  int max_y = 0;
  double center_px_x = 0.0;
  double center_px_y = 0.0;
  bool valid = false;
};

TileView s_cache;

bool ensureFileSystem() {
  if (s_fs_ready) return true;
  s_fs_ready = LittleFS.begin(true);
  if (!s_fs_ready) Serial.println("map: LittleFS mount failed");
  return s_fs_ready;
}

void closePngFile() {
  if (s_png_file) s_png_file.close();
}

void clearTileFiles() {
  closePngFile();
  if (!ensureFileSystem()) return;
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return;
  File file = root.openNextFile();
  while (file) {
    const String name = file.name();
    file.close();
    if (name.startsWith(kTilePrefix)) LittleFS.remove(name);
    file = root.openNextFile();
  }
  root.close();
}

void* pngOpen(const char* filename, int32_t* size) {
  closePngFile();
  s_png_file = LittleFS.open(filename, "r");
  if (!s_png_file) {
    *size = 0;
    return nullptr;
  }
  *size = static_cast<int32_t>(s_png_file.size());
  return &s_png_file;
}

void pngClose(void* handle) {
  File* file = static_cast<File*>(handle);
  if (file && *file) file->close();
}

int32_t pngRead(PNGFILE* handle, uint8_t* buffer, int32_t length) {
  if (!handle || !handle->fHandle) return 0;
  File* file = static_cast<File*>(handle->fHandle);
  if (!file || !*file) return 0;
  return static_cast<int32_t>(file->read(buffer, length));
}

int32_t pngSeek(PNGFILE* handle, int32_t position) {
  if (!handle || !handle->fHandle) return 0;
  File* file = static_cast<File*>(handle->fHandle);
  if (!file || !*file) return 0;
  return file->seek(position) ? position : -1;
}

int pngDraw(PNGDRAW* draw) {
  if (!s_decode_target || !draw || draw->iWidth > kTileSizePx) return 0;
  s_png.getLineAsRGB565(draw, s_png_line, PNG_RGB565_LITTLE_ENDIAN,
                        0xffffffff);

  const int dest_x = s_decode_origin_x;
  const int dest_y = s_decode_origin_y + draw->y;
  if (dest_y < 0 || dest_y >= kMapSizePx || dest_x >= kMapSizePx ||
      dest_x + draw->iWidth <= 0) {
    return 1;
  }

  const int src_x = std::max(0, -dest_x);
  const int clipped_x = std::max(0, dest_x);
  const int clipped_w =
      std::min(draw->iWidth - src_x, kMapSizePx - clipped_x);
  if (clipped_w > 0) {
    s_decode_target->pushImage(clipped_x, dest_y, clipped_w, 1,
                               s_png_line + src_x);
  }
  return 1;
}

int zoomForRange(double lat, float outer_radius_km) {
  constexpr double kMetersPerPixelZoom0 = 156543.03392804097;
  const double width_m =
      std::max(1000.0, static_cast<double>(outer_radius_km) * 2000.0);
  const double zoom = std::log2(kMapSizePx * kMetersPerPixelZoom0 *
                                std::cos(lat * 0.017453292519943295) / width_m);
  return static_cast<int>(std::max(1.0, std::min(18.0, std::round(zoom))));
}

TileView tileViewFor(double lat, double lon, float outer_radius_km) {
  TileView view;
  view.zoom = zoomForRange(lat, outer_radius_km);
  const double world_px = static_cast<double>(1UL << view.zoom) * kTileSizePx;
  const double clipped_lat = std::max(-85.05112878, std::min(85.05112878, lat));
  const double lat_rad = clipped_lat * 0.017453292519943295;

  view.center_px_x = (lon + 180.0) / 360.0 * world_px;
  view.center_px_y =
      (1.0 - std::asinh(std::tan(lat_rad)) / M_PI) * 0.5 * world_px;

  const double left = view.center_px_x - kMapSizePx * 0.5;
  const double top = view.center_px_y - kMapSizePx * 0.5;
  view.min_x = static_cast<int>(std::floor(left / kTileSizePx));
  view.max_x =
      static_cast<int>(std::floor((left + kMapSizePx - 1) / kTileSizePx));
  view.min_y = static_cast<int>(std::floor(top / kTileSizePx));
  view.max_y =
      static_cast<int>(std::floor((top + kMapSizePx - 1) / kTileSizePx));
  view.valid = true;
  return view;
}

bool sameView(const TileView& a, const TileView& b) {
  return a.valid && b.valid && a.zoom == b.zoom && a.min_x == b.min_x &&
         a.max_x == b.max_x && a.min_y == b.min_y && a.max_y == b.max_y &&
         std::fabs(a.center_px_x - b.center_px_x) < 0.5 &&
         std::fabs(a.center_px_y - b.center_px_y) < 0.5;
}

int wrapTileX(int x, int zoom) {
  const int count = 1 << zoom;
  x %= count;
  return x < 0 ? x + count : x;
}

String tilePath(int zoom, int x, int y) {
  return String(kTilePrefix) + zoom + '-' + x + '-' + y + ".png";
}

String tileUrl(int zoom, int x, int y) {
  return String("https://api.maptiler.com/maps/") + kMapStyle + "/256/" +
         zoom + '/' + x + '/' + y + ".png?key=" + s_api_key;
}

bool validPngFile(const String& path, bool log_failure) {
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  uint8_t signature[sizeof(kPngSignature)] = {};
  const size_t read = file.read(signature, sizeof(signature));
  file.close();
  const bool valid = read == sizeof(signature) &&
                     std::memcmp(signature, kPngSignature,
                                 sizeof(kPngSignature)) == 0;
  if (!valid && log_failure) {
    Serial.printf("map: invalid PNG signature %s\n", path.c_str());
  }
  return valid;
}

bool allTilesCached(const TileView& view) {
  if (!ensureFileSystem() || s_invalidated || !sameView(view, s_cache)) {
    return false;
  }
  const int tile_count = 1 << view.zoom;
  for (int raw_y = view.min_y; raw_y <= view.max_y; ++raw_y) {
    if (raw_y < 0 || raw_y >= tile_count) return false;
    for (int raw_x = view.min_x; raw_x <= view.max_x; ++raw_x) {
      const int x = wrapTileX(raw_x, view.zoom);
      if (!validPngFile(tilePath(view.zoom, x, raw_y), false)) return false;
    }
  }
  return true;
}

bool downloadTile(int zoom, int x, int y, const String& path) {
  Serial.printf("map: TLS heap before tile %u, largest %u\n", ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, tileUrl(zoom, x, y))) {
    Serial.println("map: tile HTTP begin failed");
    return false;
  }
  http.setTimeout(15000);
  http.setReuse(false);
  http.addHeader("Accept", "image/png");
  http.addHeader("Accept-Encoding", "identity");

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("map: tile %d/%d/%d HTTP %d\n", zoom, x, y, status);
    http.end();
    return false;
  }

  const String content_type = http.header("Content-Type");
  const int content_length = http.getSize();
  if (content_length <= 0 ||
      static_cast<size_t>(content_length) > kMaxTileBytes) {
    Serial.printf("map: tile %d/%d/%d invalid size %d type %s\n", zoom, x, y,
                  content_length, content_type.c_str());
    http.end();
    return false;
  }

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("map: tile cache open failed");
    http.end();
    return false;
  }
  const int written = http.writeToStream(&file);
  file.close();
  http.end();

  if (written != content_length || !validPngFile(path, true)) {
    Serial.printf("map: rejected tile %d/%d/%d (%d/%d bytes)\n", zoom, x, y,
                  written, content_length);
    LittleFS.remove(path);
    return false;
  }

  Serial.printf("map: cached PNG tile %d/%d/%d (%d bytes, %s)\n", zoom, x, y,
                written, content_type.c_str());
  return true;
}

bool ensureTiles(const TileView& view) {
  if (WiFi.status() != WL_CONNECTED || !ensureFileSystem()) return false;
  if (!sameView(view, s_cache) || s_invalidated) {
    clearTileFiles();
    s_cache = TileView{};
  }

  const int tile_count = 1 << view.zoom;
  for (int raw_y = view.min_y; raw_y <= view.max_y; ++raw_y) {
    if (raw_y < 0 || raw_y >= tile_count) return false;
    for (int raw_x = view.min_x; raw_x <= view.max_x; ++raw_x) {
      const int x = wrapTileX(raw_x, view.zoom);
      const String path = tilePath(view.zoom, x, raw_y);
      if (!validPngFile(path, false)) {
        LittleFS.remove(path);
        if (!downloadTile(view.zoom, x, raw_y, path)) return false;
      }
      delay(1);
    }
  }

  s_cache = view;
  s_invalidated = false;
  s_next_retry_ms = 0;
  return true;
}

bool decodeTile(lgfx::LovyanGFX& target, const String& path, int origin_x,
                int origin_y) {
  if (!validPngFile(path, true)) {
    LittleFS.remove(path);
    return false;
  }
  s_decode_target = &target;
  s_decode_origin_x = origin_x;
  s_decode_origin_y = origin_y;

  const int open_result =
      s_png.open(path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (open_result != PNG_SUCCESS) {
    closePngFile();
    s_decode_target = nullptr;
    Serial.printf("map: tile PNG open failed %s (%d)\n", path.c_str(),
                  open_result);
    LittleFS.remove(path);
    return false;
  }

  const int result = s_png.decode(nullptr, 0);
  s_png.close();
  closePngFile();
  s_decode_target = nullptr;
  if (result != PNG_SUCCESS) {
    Serial.printf("map: tile PNG decode failed %s (%d)\n", path.c_str(),
                  result);
    LittleFS.remove(path);
    return false;
  }
  return true;
}

bool drawTiles(lgfx::LovyanGFX& target, const TileView& view) {
  const double left = view.center_px_x - kMapSizePx * 0.5;
  const double top = view.center_px_y - kMapSizePx * 0.5;
  for (int raw_y = view.min_y; raw_y <= view.max_y; ++raw_y) {
    for (int raw_x = view.min_x; raw_x <= view.max_x; ++raw_x) {
      const int x = wrapTileX(raw_x, view.zoom);
      const int origin_x =
          static_cast<int>(std::lround(raw_x * kTileSizePx - left));
      const int origin_y =
          static_cast<int>(std::lround(raw_y * kTileSizePx - top));
      if (!decodeTile(target, tilePath(view.zoom, x, raw_y), origin_x,
                      origin_y)) {
        return false;
      }
    }
  }
  return true;
}

bool drawPrepared(lgfx::LovyanGFX& target, double lat, double lon,
                  float outer_radius_km, bool allow_download) {
  init();
  if (!configured()) return false;
  const unsigned long now = millis();
  if (s_next_retry_ms && static_cast<long>(now - s_next_retry_ms) < 0) {
    return false;
  }

  const TileView view = tileViewFor(lat, lon, outer_radius_km);
  if (!allTilesCached(view)) {
    if (!allow_download || !ensureTiles(view)) {
      s_next_retry_ms = now + kRetryDelayMs;
      return false;
    }
  }
  if (!drawTiles(target, view)) {
    s_next_retry_ms = now + kRetryDelayMs;
    return false;
  }
  return true;
}

}  // namespace

void init() {
  if (s_initialized) return;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, true)) {
    prefs.getString(kPrefsApiKey, "")
        .substring(0, kMaxApiKeyLen)
        .toCharArray(s_api_key, sizeof(s_api_key));
    prefs.end();
  }
  ensureFileSystem();
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
  std::strncpy(s_api_key, key ? key : "", kMaxApiKeyLen);
  s_api_key[kMaxApiKeyLen] = '\0';
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    if (s_api_key[0] == '\0')
      prefs.remove(kPrefsApiKey);
    else
      prefs.putString(kPrefsApiKey, s_api_key);
    prefs.end();
  }
  invalidate();
}

void clear() {
  saveApiKey("");
  clearTileFiles();
}

void invalidate() {
  s_invalidated = true;
  s_cache = TileView{};
  s_next_retry_ms = 0;
}

bool draw(LGFX_Sprite& target, double lat, double lon, float outer_radius_km) {
  init();
  if (!configured()) return false;
  const unsigned long now = millis();
  if (s_next_retry_ms && static_cast<long>(now - s_next_retry_ms) < 0) {
    return false;
  }

  const TileView view = tileViewFor(lat, lon, outer_radius_km);
  if (!allTilesCached(view)) {
    target.deleteSprite();
    Serial.printf("map: released radar sprite; heap %u largest %u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    const bool downloaded = ensureTiles(view);
    target.setColorDepth(8);
    const bool recreated = target.createSprite(kMapSizePx, kMapSizePx);
    if (!downloaded || !recreated) {
      if (!recreated) {
        Serial.printf("map: radar sprite recreate failed; heap %u largest %u\n",
                      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      }
      s_next_retry_ms = now + kRetryDelayMs;
      return false;
    }
  }

  if (!drawTiles(target, view)) {
    s_next_retry_ms = now + kRetryDelayMs;
    return false;
  }
  return true;
}

bool drawDirect(lgfx::LovyanGFX& target, double lat, double lon,
                float outer_radius_km) {
  Serial.printf("map: direct LCD render; heap %u largest %u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return drawPrepared(target, lat, lon, outer_radius_km, true);
}

}  // namespace services::map_background
