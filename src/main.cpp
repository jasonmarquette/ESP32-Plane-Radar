/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include <cmath>
#include <cstdint>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/map_background.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

constexpr unsigned long kDisplayMinRefreshMs = 10000UL;
constexpr unsigned long kDisplayMaxRefreshMs = 30000UL;

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_display_refresh_ms = 0;
uint32_t g_last_traffic_signature = 0;

uint32_t mixHash(uint32_t hash, uint32_t value) {
  hash ^= value;
  hash *= 16777619UL;
  return hash;
}

uint32_t trafficSignature() {
  uint32_t hash = 2166136261UL;
  const size_t count = services::adsb::aircraftCount();
  const services::adsb::Aircraft* planes = services::adsb::aircraftList();

  hash = mixHash(hash, static_cast<uint32_t>(count));
  for (size_t i = 0; i < count; ++i) {
    // Quantize position to roughly 1 km. Small ADS-B jitter no longer causes a
    // complete screen redraw every poll, while meaningful movement still does.
    const int32_t lat_bucket =
        static_cast<int32_t>(lroundf(planes[i].lat * 100.0f));
    const int32_t lon_bucket =
        static_cast<int32_t>(lroundf(planes[i].lon * 100.0f));
    hash = mixHash(hash, static_cast<uint32_t>(lat_bucket));
    hash = mixHash(hash, static_cast<uint32_t>(lon_bucket));

    for (size_t c = 0; c < sizeof(planes[i].callsign) &&
                       planes[i].callsign[c] != '\0';
         ++c) {
      hash = mixHash(hash,
                     static_cast<uint8_t>(planes[i].callsign[c]));
    }
  }
  return hash;
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_last_display_refresh_ms = millis();
  g_last_traffic_signature = trafficSignature();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  services::map_background::invalidate();

  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
    g_last_display_refresh_ms = millis();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) onRangeTap();
}

void fetchAndDrawAircraft() {
  const float configured_radius = services::location::adsbRadiusKm();
  const float fetch_km =
      configured_radius > 0.0f ? configured_radius : ui::radar::fetchRadiusKm();

  ui::radarDisplayReleaseFrameForNetwork();
  wifiSuspendLanPortal();

  const bool fetched = services::adsb::fetchUpdate(
      services::location::lat(), services::location::lon(), fetch_km);

  if (fetched) {
    const unsigned long now = millis();
    const uint32_t signature = trafficSignature();
    const bool materially_changed = signature != g_last_traffic_signature;
    const bool minimum_elapsed =
        now - g_last_display_refresh_ms >= kDisplayMinRefreshMs;
    const bool maximum_elapsed =
        now - g_last_display_refresh_ms >= kDisplayMaxRefreshMs;

    if ((materially_changed && minimum_elapsed) || maximum_elapsed) {
      ui::radarDisplayRefreshAircraft();
      g_last_display_refresh_ms = millis();
      g_last_traffic_signature = signature;
    }
  }

  wifiResumeLanPortal();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) statusScreenPortal();
  services::location::init();
  services::map_background::init();
  ui::radar::rangeInit();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) showRadarIfConnected();
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) g_wifi_down_since = millis();

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
