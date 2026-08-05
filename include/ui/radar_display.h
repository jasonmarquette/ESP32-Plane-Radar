#pragma once

namespace ui {

/** Draw the complete radar display. */
void radarDisplayDraw();

/** Redraw the radar after new aircraft data arrives. */
void radarDisplayRefreshAircraft();

/**
 * Release the temporary 320x320 frame buffer before TLS/network work.
 * The next draw recreates it only long enough to compose and push one frame.
 */
void radarDisplayReleaseFrameForNetwork();

}  // namespace ui
