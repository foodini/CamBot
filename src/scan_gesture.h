#pragma once

// The right-click "scan" gesture: hold the right button down on a bar and drag sideways, and the
// further from where you pressed, the faster the video (or telemetry) runs that way. This is the
// curve that turns the drag distance into a rate.

namespace scan_gesture {

// Drag distances are in the units of InteractionMgr::mouse_x_pos(): the whole width of the canvas
// is 2.0 across, so 1920 pixels of a 1920-pixel-wide canvas is 2.0.
const float DEADZONE = 0.01f;              // No motion below this -- avoids drift from a near-stationary click.
const float MAX_DRAG = 0.5f;               // Offset at which the rate saturates at the max speed.
const float MIN_RATE_PER_SECOND = 5.0f;    // Rate right at the edge of the deadzone.
const float MAX_SPEED_MULTIPLIER = 50.0f;  // Rate at MAX_DRAG, as a multiple of the native rate.

// A signed drag offset in, a signed rate out, in native units (frames, or telemetry samples) per
// second; positive scans forward. `native_rate` is the units per second at normal speed.
float rate_for_offset(float drag, float native_rate);

// Pixels of a window dragged -> the units above, so the feel is the same however big the window
// is: a drag of this many pixels counts as 2.0 across.
const float REFERENCE_WIDTH_PIXELS = 1920.0f;
float drag_for_pixels(float pixels);

}  // namespace scan_gesture
