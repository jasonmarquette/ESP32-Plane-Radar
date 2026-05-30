#include "ui/radar_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <cstdlib>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "ui/radar_range.h"
#include "ui/radar_theme.h"

namespace fonts = lgfx::v1::fonts;

namespace ui {
namespace radar {

uint16_t kColorBackground = 0x0000;
uint16_t kColorGrid = 0x0320;
uint16_t kColorLabel = 0xFFFF;
uint16_t kColorCenter = 0xFFFF;

}  // namespace radar

namespace {

bool s_label_metrics_ready = false;
bool s_cardinal_use_vlw = false;
bool s_scale_use_vlw = false;
float s_cardinal_vlw_size = 0.56f;
float s_scale_vlw_size = 0.50f;
const lgfx::GFXfont* s_cardinal_gfx = &fonts::FreeSansBold12pt7b;
const lgfx::GFXfont* s_scale_gfx = &fonts::FreeSansBold9pt7b;

int s_scale_label_max_w = 0;
int s_scale_label_h = 0;

int absDiff(int a, int b) { return std::abs(a - b); }

int measureGfxHeight(const lgfx::GFXfont& font) {
  tft.setFont(&font);
  tft.setTextSize(1);
  return tft.fontHeight();
}

int measureVlwHeight(float size) {
  tft.setTextSize(size);
  return tft.fontHeight();
}

float findVlwSizeForHeight(int target_px) {
  float lo = 0.25f;
  float hi = 1.2f;
  for (int i = 0; i < 16; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void applyScaleStyle();

const lgfx::GFXfont* pickGfxFontClosest(
    int target_px, const lgfx::GFXfont* const* candidates, size_t count) {
  const lgfx::GFXfont* best = candidates[0];
  int best_diff = absDiff(measureGfxHeight(*best), target_px);

  for (size_t i = 1; i < count; ++i) {
    const int diff = absDiff(measureGfxHeight(*candidates[i]), target_px);
    if (diff < best_diff) {
      best_diff = diff;
      best = candidates[i];
    }
  }
  return best;
}

void initLabelMetrics() {
  if (s_label_metrics_ready) {
    return;
  }

  const int cardinal_target = radar::kCardinalLabelHeightPx;

  if (displayFontIsSmooth()) {
    s_cardinal_use_vlw = true;
    s_cardinal_vlw_size = findVlwSizeForHeight(cardinal_target);
    const int cardinal_h = measureVlwHeight(s_cardinal_vlw_size);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    s_scale_use_vlw = true;
    s_scale_vlw_size = findVlwSizeForHeight(scale_target);
  } else {
    const lgfx::GFXfont* cardinal_candidates[] = {&fonts::FreeSansBold12pt7b,
                                                  &fonts::FreeSansBold9pt7b};
    s_cardinal_gfx =
        pickGfxFontClosest(cardinal_target, cardinal_candidates, 2);
    s_cardinal_use_vlw = false;

    const int cardinal_h = measureGfxHeight(*s_cardinal_gfx);
    const int scale_target = cardinal_h - radar::kScaleBelowCardinalPx;
    const lgfx::GFXfont* scale_candidates[] = {&fonts::FreeSansBold9pt7b,
                                               &fonts::FreeSansBold12pt7b};
    s_scale_gfx = pickGfxFontClosest(scale_target, scale_candidates, 2);
    s_scale_use_vlw = false;
  }

  applyScaleStyle();
  s_scale_label_h = tft.fontHeight();
  s_scale_label_max_w = 0;
  for (size_t i = 0; i < radar::kRangePresetCount; ++i) {
    const int w = tft.textWidth(radar::kRangePresets[i].ring3_label);
    if (w > s_scale_label_max_w) {
      s_scale_label_max_w = w;
    }
  }

  s_label_metrics_ready = true;
}

void initPalette() {
  radar::kColorBackground = tft.color565(0, 0, 0);
  radar::kColorGrid = tft.color565(0, 120, 0);
  radar::kColorLabel = tft.color565(255, 255, 255);
  radar::kColorCenter = tft.color565(255, 255, 255);
}

void applyCardinalStyle() {
  if (s_cardinal_use_vlw) {
    tft.setTextSize(s_cardinal_vlw_size);
  } else {
    tft.setFont(s_cardinal_gfx);
    tft.setTextSize(1);
  }
}

void applyScaleStyle() {
  if (s_scale_use_vlw) {
    tft.setTextSize(s_scale_vlw_size);
  } else {
    tft.setFont(s_scale_gfx);
    tft.setTextSize(1);
  }
}

void drawCardinalLabel(const char* text, int x, int y, textdatum_t datum) {
  applyCardinalStyle();
  tft.setTextDatum(datum);
  tft.setTextColor(radar::kColorLabel, radar::kColorBackground);
  tft.drawString(text, x, y);
}

void drawScaleLabelWithBackground(const char* text, int x, int y) {
  applyScaleStyle();
  tft.setTextDatum(textdatum_t::middle_right);

  const int tw = tft.textWidth(text);
  const int th = tft.fontHeight();
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;

  const int left = x - tw - kPadX;
  const int top = y - th / 2 - kPadY;

  tft.fillRect(left, top, tw + kPadX * 2, th + kPadY * 2,
               radar::kColorBackground);
  tft.setTextColor(radar::kColorGrid, radar::kColorBackground);
  tft.drawString(text, x, y);
}

void drawCircleThick(int cx, int cy, int r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  if (r > 0) {
    tft.drawCircle(cx, cy, r - 1, color);
  }
}

void drawRings(int cx, int cy, int outer_radius) {
  for (int i = 1; i <= radar::kRingCount; ++i) {
    const int r = (outer_radius * i) / radar::kRingCount;
    drawCircleThick(cx, cy, r, radar::kColorGrid);
  }
}

void drawCrosshairs(int cx, int cy, int radius, uint16_t color) {
  tft.drawLine(cx - 1, cy - radius, cx - 1, cy + radius, color);
  tft.drawLine(cx, cy - radius, cx, cy + radius, color);
  tft.drawLine(cx - radius, cy - 1, cx + radius, cy - 1, color);
  tft.drawLine(cx - radius, cy, cx + radius, cy, color);
}

void drawCenterDot(int cx, int cy) {
  tft.fillCircle(cx, cy, radar::kCenterDotRadius, radar::kColorCenter);
}

void drawCardinalLabels() {
  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int edge = radar::kSize - 1;

  drawCardinalLabel("N", cx, radar::kCardinalNorthOffsetY, textdatum_t::top_center);
  drawCardinalLabel("S", cx, edge + radar::kCardinalSouthOffsetY,
                    textdatum_t::bottom_center);
  drawCardinalLabel("W", 0, cy, textdatum_t::middle_left);
  drawCardinalLabel("E", edge, cy, textdatum_t::middle_right);
}

int scaleLabelAnchorX(int cx, int outer_radius) {
  return cx + outer_radius - radar::kScaleGapFromOuterRing;
}

void drawScaleLabel(int cx, int cy, int outer_radius) {
  drawScaleLabelWithBackground(radar::rangeCurrent().ring3_label,
                               scaleLabelAnchorX(cx, outer_radius), cy);
}

void repairGridInScaleLabelArea(int cx, int cy, int outer_radius, int left, int top,
                                int w, int h) {
  const int right = left + w;
  const int bottom = top + h;
  const uint16_t grid = radar::kColorGrid;

  if (cy >= top && cy < bottom) {
    tft.drawLine(cx - outer_radius, cy, cx + outer_radius, cy, grid);
    tft.drawLine(cx - outer_radius, cy - 1, cx + outer_radius, cy - 1, grid);
  }

  const int ring_x = cx + outer_radius;
  if (ring_x >= left && ring_x < right && cy >= top && cy < bottom) {
    drawCircleThick(cx, cy, outer_radius, grid);
  }
}

void eraseScaleLabelArea(int anchor_x, int anchor_y) {
  constexpr int kPadX = 3;
  constexpr int kPadY = 2;
  const int w = s_scale_label_max_w + kPadX * 2;
  const int h = s_scale_label_h + kPadY * 2;
  const int left = anchor_x - w;
  const int top = anchor_y - h / 2;

  tft.fillRect(left, top, w, h, radar::kColorBackground);
  repairGridInScaleLabelArea(radar::kCenterX, radar::kCenterY,
                             radar::kGridOuterRadius, left, top, w, h);
}

}  // namespace

void radarDisplayDraw() {
  initPalette();
  initLabelMetrics();

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int grid_r = radar::kGridOuterRadius;

  tft.fillScreen(radar::kColorBackground);

  drawRings(cx, cy, grid_r);
  drawCrosshairs(cx, cy, grid_r, radar::kColorGrid);
  drawCenterDot(cx, cy);
  drawCardinalLabels();
  drawScaleLabel(cx, cy, grid_r);

  tft.setTextDatum(textdatum_t::top_left);
}

void radarDisplayRefreshRange() {
  initPalette();
  initLabelMetrics();

  const int cx = radar::kCenterX;
  const int cy = radar::kCenterY;
  const int anchor_x = scaleLabelAnchorX(cx, radar::kGridOuterRadius);

  eraseScaleLabelArea(anchor_x, cy);
  drawScaleLabel(cx, cy, radar::kGridOuterRadius);

  tft.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui
