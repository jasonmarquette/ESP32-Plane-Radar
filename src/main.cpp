/**
 * Round-screen — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;

bool g_boot_down = false;
unsigned long g_boot_down_at = 0;
bool g_boot_long_press_handled = false;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void handleBootButton() {
  if (wifiBootButtonPressed()) {
    if (!g_boot_down) {
      g_boot_down = true;
      g_boot_down_at = millis();
      g_boot_long_press_handled = false;
    } else if (!g_boot_long_press_handled &&
               millis() - g_boot_down_at >= config::kBootResetHoldMs) {
      g_boot_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
    return;
  }

  if (!g_boot_down) {
    return;
  }

  const unsigned long held_ms = millis() - g_boot_down_at;
  g_boot_down = false;

  if (g_boot_long_press_handled) {
    return;
  }

  if (held_ms < config::kBootTapMinMs || held_ms >= config::kBootResetHoldMs) {
    return;
  }

  ui::radar::rangeNext();
  Serial.printf("Range: %s (outer ~%.0f km)\n", ui::radar::rangeCurrent().ring3_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayRefreshRange();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("round-screen");

  displayInit();
  ui::radar::rangeInit();
  wifiClearCredentialsIfBootHeld();

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

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
    }
  }

  delay(50);
}
