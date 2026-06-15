#include "hardware/display.h"

#include "hardware/display_font.h"

LGFX tft;

void displayInit() {
  tft.init();
  tft.setRotation(1);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
