#pragma once

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw only the range label (no full-screen clear). Use after rangeNext(). */
void radarDisplayRefreshRange();

}  // namespace ui
