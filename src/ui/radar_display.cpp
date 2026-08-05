#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/map_background.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"
#include "ui/runway_overlay.h"

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;
uint16_t kColorAircraft = 0x001F;
uint16_t kColorTrackVector = 0xFFFF;
uint16_t kColorTagType = 0x5DFF;
uint16_t kColorTagAltitude = 0xFFE0;
uint16_t kColorRunway = 0x4D5F;
uint16_t kColorRunwayLabel = 0x7DFF;

}  // namespace radar

namespace {

constexpr float kKmPerDegLat = 111.0f;
constexpr size_t kNoTarget = static_cast<size_t>(-1);

LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;
size_t s_target_index = kNoTarget;
float s_target_distance_km = 0.0f;

void initPalette() {
  radar::kColorBackground = tft.color565(radar::kBgR, radar::kBgG, radar::kBgB);
  radar::kColorGrid = tft.color565(radar::kGridR, radar::kGridG, radar::kGridB);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);

  if (config::kDisplayRgbOrder) {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftB, radar::kAircraftG, radar::kAircraftR);
  } else {
    radar::kColorAircraft =
        tft.color565(radar::kAircraftR, radar::kAircraftG, radar::kAircraftB);
  }

  radar::kColorTrackVector =
      tft.color565(radar::kTrackR, radar::kTrackG, radar::kTrackB);
  radar::kColorTagType =
      tft.color565(radar::kTagTypeR, radar::kTagTypeG, radar::kTagTypeB);
  radar::kColorTagAltitude =
      tft.color565(radar::kTagAltR, radar::kTagAltG, radar::kTagAltB);
  radar::kColorRunway =
      tft.color565(radar::kRunwayR, radar::kRunwayG, radar::kRunwayB);
  radar::kColorRunwayLabel =
      tft.color565(radar::kRunwayLabelR, radar::kRunwayLabelG,
                   radar::kRunwayLabelB);
}

void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* distance_km) {
  const double center_lat = services::location::lat();
  const double center_lon = services::location::lon();
  const float lon_scale =
      kKmPerDegLat * cosf(static_cast<float>(center_lat) * 0.01745329252f);

  *dx_km = static_cast<float>(lon - center_lon) * lon_scale;
  *dy_km = static_cast<float>(lat - center_lat) * kKmPerDegLat;
  *distance_km = sqrtf(*dx_km * *dx_km + *dy_km * *dy_km);
}

void selectNearestAircraft() {
  s_target_index = kNoTarget;
  s_target_distance_km = 0.0f;

  const size_t count = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  float nearest = 1.0e9f;

  for (size_t i = 0; i < count; ++i) {
    if (!std::isfinite(planes[i].lat) || !std::isfinite(planes[i].lon)) {
      continue;
    }
    float dx = 0.0f;
    float dy = 0.0f;
    float distance = 0.0f;
    offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx, &dy, &distance);
    if (distance < nearest) {
      nearest = distance;
      s_target_index = i;
      s_target_distance_km = distance;
    }
  }
}

bool ensureFrameSprite() {
  if (s_frame_ready && s_frame.getBuffer() != nullptr) return true;
  s_frame.setColorDepth(8);
  if (!s_frame.createSprite(radar::kSize, radar::kSize)) {
    Serial.printf("radar: frame sprite alloc failed; using direct renderer; heap %u largest %u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    s_frame_ready = false;
    return false;
  }
  s_frame_ready = true;
  return true;
}

void latLonToScreen(float lat, float lon, int* x, int* y,
                    float* distance_km = nullptr) {
  float dx = 0.0f;
  float dy = 0.0f;
  float distance = 0.0f;
  offsetKmFromCenter(lat, lon, &dx, &dy, &distance);
  const float px_per_km = static_cast<float>(radar::kGridOuterRadius) /
                          radar::rangeCurrent().outer_km;
  *x = radar::kCenterX + static_cast<int>(lroundf(dx * px_per_km));
  *y = radar::kCenterY - static_cast<int>(lroundf(dy * px_per_km));
  if (distance_km) *distance_km = distance;
}

void drawRing(lgfx::LovyanGFX& gfx, int radius) {
  const int thickness =
      std::max(1, static_cast<int>(radar::kGridStrokeHalfWidth * 2.0f));
  for (int i = 0; i < thickness && radius - i > 0; ++i) {
    gfx.drawCircle(radar::kCenterX, radar::kCenterY, radius - i,
                   radar::kColorGrid);
  }
}

void drawCardinal(lgfx::LovyanGFX& gfx, const char* text, int x, int y,
                  textdatum_t datum) {
  gfx.setFont(&fonts::FreeSansBold12pt7b);
  gfx.setTextSize(1);
  gfx.setTextDatum(datum);
  gfx.setTextColor(radar::kColorLabel);
  gfx.drawString(text, x, y);
}

void drawGrid(lgfx::LovyanGFX& gfx, bool map_ready) {
  if (!map_ready) gfx.fillRect(0, 0, radar::kSize, radar::kSize,
                               radar::kColorBackground);

  for (int i = 1; i <= radar::kRingCount; ++i) {
    drawRing(gfx, radar::kGridOuterRadius * i / radar::kRingCount);
  }

  gfx.drawWideLine(radar::kCenterX, radar::kCenterY - radar::kGridOuterRadius,
                   radar::kCenterX, radar::kCenterY + radar::kGridOuterRadius,
                   radar::kGridStrokeHalfWidth, radar::kColorGrid);
  gfx.drawWideLine(radar::kCenterX - radar::kGridOuterRadius,
                   radar::kCenterY,
                   radar::kCenterX + radar::kGridOuterRadius,
                   radar::kCenterY, radar::kGridStrokeHalfWidth,
                   radar::kColorGrid);

  runway::drawLargeAirportRunways(gfx);
  gfx.fillSmoothCircle(radar::kCenterX, radar::kCenterY,
                       radar::kCenterDotRadius, radar::kColorCenter);

  drawCardinal(gfx, "N", radar::kCenterX, 0, textdatum_t::top_center);
  drawCardinal(gfx, "S", radar::kCenterX, radar::kSize - 1,
               textdatum_t::bottom_center);
  drawCardinal(gfx, "W", 0, radar::kCenterY, textdatum_t::middle_left);
  drawCardinal(gfx, "E", radar::kSize - 1, radar::kCenterY,
               textdatum_t::middle_right);

  char range_label[12];
  radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  gfx.setFont(&fonts::Font2);
  gfx.setTextSize(1);
  gfx.setTextDatum(textdatum_t::middle_right);
  const int label_x = radar::kCenterX + radar::kGridOuterRadius - 4;
  const int width = gfx.textWidth(range_label);
  gfx.fillRect(label_x - width - 3, radar::kCenterY - 8, width + 6, 16,
               radar::kColorBackground);
  gfx.setTextColor(radar::kColorGrid, radar::kColorBackground);
  gfx.drawString(range_label, label_x, radar::kCenterY);
}

void trianglePoints(int cx, int cy, float heading_deg, int nose_len,
                    int tail_len, int half_width, int* tip_x, int* tip_y,
                    int* left_x, int* left_y, int* right_x, int* right_y) {
  const float rad = heading_deg * 0.01745329252f;
  const float sin_h = sinf(rad);
  const float cos_h = cosf(rad);
  *tip_x = cx + static_cast<int>(lroundf(sin_h * nose_len));
  *tip_y = cy - static_cast<int>(lroundf(cos_h * nose_len));
  const int base_x = cx - static_cast<int>(lroundf(sin_h * tail_len));
  const int base_y = cy + static_cast<int>(lroundf(cos_h * tail_len));
  const int wing_x = static_cast<int>(lroundf(cos_h * half_width));
  const int wing_y = static_cast<int>(lroundf(sin_h * half_width));
  *left_x = base_x + wing_x;
  *left_y = base_y + wing_y;
  *right_x = base_x - wing_x;
  *right_y = base_y - wing_y;
}

void drawAircraftSymbol(lgfx::LovyanGFX& gfx, int x, int y,
                        const services::adsb::Aircraft& plane,
                        bool selected) {
  const int nose = radar::kAircraftNoseLenPx + (selected ? 3 : 0);
  const int tail = radar::kAircraftTailLenPx + (selected ? 1 : 0);
  const int half = radar::kAircraftTailHalfPx + (selected ? 2 : 0);
  int tx, ty, lx, ly, rx, ry;
  trianglePoints(x, y, plane.nose_deg, nose, tail, half, &tx, &ty, &lx, &ly,
                 &rx, &ry);
  if (selected) {
    gfx.fillSmoothCircle(x, y, half + 5, radar::kColorLabel);
    gfx.fillSmoothCircle(x, y, half + 2, radar::kColorBackground);
  }
  gfx.fillTriangle(tx, ty, lx, ly, rx, ry,
                   selected ? radar::kColorLabel : radar::kColorAircraft);
}

void drawTrackVector(lgfx::LovyanGFX& gfx, int x, int y,
                     const services::adsb::Aircraft& plane, bool selected) {
  if (plane.gs_knots <= 0.0f) return;
  const float px = plane.gs_knots * 1.852f * radar::kAircraftTrackHorizonSec /
                   3600.0f * radar::kGridOuterRadius /
                   radar::kAircraftTrackRefOuterKm *
                   radar::kAircraftTrackLengthScale;
  const int length = std::max(radar::kAircraftSpeedLineMinPx,
                              static_cast<int>(lroundf(px)));
  const float rad = plane.track_deg * 0.01745329252f;
  const int ex = x + static_cast<int>(lroundf(sinf(rad) * length));
  const int ey = y - static_cast<int>(lroundf(cosf(rad) * length));
  gfx.drawWideLine(x, y, ex, ey,
                   selected ? radar::kAircraftTrackLineHalfWidth + 1.0f
                            : radar::kAircraftTrackLineHalfWidth,
                   selected ? radar::kColorLabel : radar::kColorTrackVector);
}

void drawAircraftTag(lgfx::LovyanGFX& gfx, int x, int y,
                     const services::adsb::Aircraft& plane, bool selected) {
  const char* primary = plane.callsign[0] ? plane.callsign : plane.type;
  if (!primary || !primary[0]) return;
  gfx.setFont(&fonts::Font2);
  gfx.setTextSize(1);
  gfx.setTextDatum(x < radar::kCenterX ? textdatum_t::top_left
                                       : textdatum_t::top_right);
  gfx.setTextColor(selected ? radar::kColorLabel : radar::kColorTagType,
                   radar::kColorBackground);
  const int offset = radar::kAircraftNoseLenPx +
                     radar::kAircraftTailHalfPx +
                     radar::kAircraftLabelGapPx + (selected ? 5 : 2);
  gfx.drawString(primary, x < radar::kCenterX ? x + offset : x - offset,
                 y - 7);
}

void drawAircraft(lgfx::LovyanGFX& gfx) {
  const size_t count = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  const float visible_km = radar::rangeCurrent().outer_km;

  for (size_t i = 0; i < count; ++i) {
    int x = 0;
    int y = 0;
    float distance = 0.0f;
    latLonToScreen(planes[i].lat, planes[i].lon, &x, &y, &distance);
    const bool selected = i == s_target_index;

    if (distance > visible_km) {
      float dx = 0.0f;
      float dy = 0.0f;
      float ignored = 0.0f;
      offsetKmFromCenter(planes[i].lat, planes[i].lon, &dx, &dy, &ignored);
      if (ignored < 0.01f) continue;
      const float bearing = atan2f(dx, dy);
      const int rim = radar::kCenterX - radar::kBeyondRingScreenMarginPx;
      const int dot_x = radar::kCenterX +
                        static_cast<int>(lroundf(sinf(bearing) * rim));
      const int dot_y = radar::kCenterY -
                        static_cast<int>(lroundf(cosf(bearing) * rim));
      gfx.fillSmoothCircle(dot_x, dot_y,
                           radar::kBeyondRingDotRadiusPx + (selected ? 2 : 0),
                           selected ? radar::kColorLabel
                                    : radar::kColorAircraft);
      continue;
    }

    drawTrackVector(gfx, x, y, planes[i], selected);
    drawAircraftSymbol(gfx, x, y, planes[i], selected);
    drawAircraftTag(gfx, x, y, planes[i], selected);
  }
}

void panelLabel(int x, int y, const char* text, uint16_t color,
                uint16_t background) {
  tft.setFont(&fonts::Font2);
  tft.setTextSize(1);
  tft.setTextColor(color, background);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawInfoPanel() {
  constexpr int panel_x = 320;
  constexpr int panel_w = 160;
  constexpr int panel_h = 320;
  const int x = panel_x + 8;
  const uint16_t bg = radar::kColorBackground;
  const uint16_t line = radar::kColorGrid;
  const uint16_t label = radar::kColorLabel;
  const uint16_t value = tft.color565(120, 255, 255);
  const uint16_t good = tft.color565(0, 255, 120);
  const uint16_t bad = tft.color565(255, 80, 80);

  tft.fillRect(panel_x, 0, panel_w, panel_h, bg);
  tft.drawFastVLine(panel_x, 0, panel_h, line);
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(textdatum_t::top_left);
  tft.setTextSize(2);
  tft.setTextColor(value, bg);
  tft.setCursor(x, 8);
  tft.print("Plane");
  tft.setCursor(x, 34);
  tft.print("Radar");
  tft.drawFastHLine(panel_x + 6, 66, panel_w - 12, line);

  char count_text[24];
  snprintf(count_text, sizeof(count_text), "TARGET  %u TOTAL",
           static_cast<unsigned>(services::adsb::aircraftCount()));
  panelLabel(x, 76, count_text, label, bg);

  const services::adsb::Aircraft* planes = services::adsb::aircraftList();
  if (s_target_index != kNoTarget &&
      s_target_index < services::adsb::aircraftCount()) {
    const services::adsb::Aircraft& target = planes[s_target_index];
    const char* name = target.callsign[0] ? target.callsign : "UNKNOWN";
    tft.setFont(&fonts::Font2);
    tft.setTextSize(2);
    tft.setTextColor(label, bg);
    tft.setCursor(x, 94);
    tft.print(name);

    char line_text[32];
    snprintf(line_text, sizeof(line_text), "TYPE  %s",
             target.type[0] ? target.type : "----");
    panelLabel(x, 124, line_text, value, bg);
    snprintf(line_text, sizeof(line_text), "DIST  %.1f km",
             s_target_distance_km);
    panelLabel(x, 144, line_text, value, bg);
    snprintf(line_text, sizeof(line_text), "ALT   %s",
             target.alt[0] ? target.alt : "----");
    panelLabel(x, 164, line_text, radar::kColorTagAltitude, bg);
    snprintf(line_text, sizeof(line_text), "SPD   %.0f kt", target.gs_knots);
    panelLabel(x, 184, line_text, value, bg);
    snprintf(line_text, sizeof(line_text), "HDG   %03.0f deg", target.track_deg);
    panelLabel(x, 204, line_text, value, bg);
  } else {
    panelLabel(x, 104, "NO TARGET", bad, bg);
    panelLabel(x, 126, "Waiting for traffic", value, bg);
  }

  tft.drawFastHLine(panel_x + 6, 230, panel_w - 12, line);
  panelLabel(x, 240, "WIFI", label, bg);
  if (WiFi.status() == WL_CONNECTED) {
    panelLabel(x, 258, "CONNECTED", good, bg);
    tft.setFont(&fonts::Font2);
    tft.setTextSize(1);
    tft.setTextColor(value, bg);
    tft.setCursor(x, 276);
    tft.print(WiFi.localIP());
  } else {
    panelLabel(x, 258, "OFFLINE", bad, bg);
  }

  char range_text[24];
  snprintf(range_text, sizeof(range_text), "RANGE %.0f km",
           radar::rangeCurrent().outer_km);
  panelLabel(x, 298, range_text, label, bg);
}

void renderFrame() {
  selectNearestAircraft();
  const bool map_ready = services::map_background::draw(
      s_frame, services::location::lat(), services::location::lon(),
      radar::rangeCurrent().outer_km);
  s_frame_ready = s_frame.getBuffer() != nullptr;
  if (!s_frame_ready && !ensureFrameSprite()) return;
  drawGrid(s_frame, map_ready);
  drawAircraft(s_frame);
  s_frame.pushSprite(0, 0);
  drawInfoPanel();
  tft.setTextDatum(textdatum_t::top_left);
}

void renderDirect() {
  selectNearestAircraft();
  tft.startWrite();
  const bool map_ready = services::map_background::drawDirect(
      tft, services::location::lat(), services::location::lon(),
      radar::rangeCurrent().outer_km);
  drawGrid(tft, map_ready);
  drawAircraft(tft);
  tft.endWrite();
  drawInfoPanel();
  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  if (ensureFrameSprite()) {
    renderFrame();
  } else {
    renderDirect();
  }
}

void radarDisplayRefreshAircraft() {
  initPalette();
  if (ensureFrameSprite()) {
    renderFrame();
  } else {
    renderDirect();
  }
}

}  // namespace ui
