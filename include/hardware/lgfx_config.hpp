#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "config.h"

/** LovyanGFX device: ILI9488 3.5" 480x320 SPI. Pin values come from config.h. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_SPI _bus;
  lgfx::Panel_ILI9488 _panel;

public:
  LGFX() {
    {
      auto cfg = _bus.config();

      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = config::kDisplaySpiWriteHz;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;

      cfg.pin_sclk = static_cast<int>(config::kDisplayPinSclk);
      cfg.pin_mosi = static_cast<int>(config::kDisplayPinMosi);
      cfg.pin_miso = -1;
      cfg.pin_dc = static_cast<int>(config::kDisplayPinDc);

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    {
      auto cfg = _panel.config();

      cfg.pin_cs = static_cast<int>(config::kDisplayPinCs);
      cfg.pin_rst = static_cast<int>(config::kDisplayPinRst);
      cfg.pin_busy = -1;

      // These match the successful standalone LovyanGFX ILI9488 test.
      // Rotation later makes this display appear as 480x320 landscape.
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;

      cfg.readable = false;
      cfg.invert = config::kDisplayInvert;
      cfg.rgb_order = config::kDisplayRgbOrder;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};