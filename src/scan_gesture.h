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

// The state of one scan in progress, for a place that has nowhere of its own to keep it (the video
// preview). begin() when the right button goes down, step() once per frame while it is held, end()
// when it lets go.
class Scan {
public:
	Scan() : m_active(false), m_origin_x(0.0f), m_owed(0.0f) {}
	bool active() const { return m_active; }
	void begin(float origin_x_pixels) { m_active = true; m_origin_x = origin_x_pixels; m_owed = 0.0f; }
	void end() { m_active = false; }
	// One frame of the scan: how many frames to move (+1 forward, -1 back, or 0). The rate is the one
	// rate_for_offset() gives for how far the mouse is from where the button went down; the frames it
	// adds up to are paid out one at a time, whatever the rate (a longer jump is what scrubbing is for).
	int step(float mouse_x_pixels, float native_rate, float seconds);

private:
	bool  m_active;
	float m_origin_x;
	float m_owed;     // Frames the rate has earned but not yet paid out (a fraction of one, either sign).
};

}  // namespace scan_gesture
