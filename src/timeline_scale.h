#pragma once

// The arithmetic of the timeline view: which stretch of the timeline is showing, how that maps to
// pixels across the strip it is drawn in, zooming and panning, and where to put the labels on its
// ruler. No drawing in here, so it can be tested.
//
// Times are ticks (see edit_model.h) and may be fractional here, since a pixel is a fraction of a
// tick when zoomed far in. "x" is pixels from the left edge of the strip.

#include <stdint.h>
#include <string>

class TimelineScale {
public:
	TimelineScale();

	// The timeline is `total_ticks` long, and can be zoomed in until one pixel is no less than
	// `min_ticks_per_pixel` (so a frame is never made absurdly wide). While the whole timeline is
	// showing (is_fitted()), it keeps showing the whole of it as the length or the width changes.
	void set_extent(int64_t total_ticks, double min_ticks_per_pixel);
	void set_width(float pixels);

	// Shows the whole timeline.
	void fit();
	bool is_fitted() const { return m_fitted; }

	double ticks_per_pixel() const { return m_ticks_per_pixel; }
	double view_start() const { return m_start; }
	double view_end() const { return m_start + (double)m_width * m_ticks_per_pixel; }

	float   tick_to_x(double ticks) const { return (float)((ticks - m_start) / m_ticks_per_pixel); }
	double  x_to_ticks(float x) const { return m_start + (double)x * m_ticks_per_pixel; }
	// The time at x, held inside the timeline (0 to its length), as a whole tick.
	int64_t x_to_timeline_ticks(float x) const;

	// `factor` above 1 zooms in, below 1 out, and what is at x stays at x.
	void zoom_at(float x, double factor);
	// Where to zoom about when the cursor is at x: x itself, except that near the start or the end of the timeline (within
	// timeline_zoom::NEAR_END_FRACTION of the strip's width of it, and only where that end is really showing) it is that end's own x, so zooming
	// in there pins the end in place instead of leaving it to be panned to.
	float zoom_anchor(float x) const;
	// Moves the view later by `pixels` (earlier if negative), staying inside the timeline.
	void pan_by_pixels(float pixels);
	// If `ticks` isn't showing, moves the view to show it a quarter of the way in from the left.
	// Returns whether it had to move.
	bool reveal(double ticks);

private:
	double fit_ticks_per_pixel() const;
	void   clamp_view();

	int64_t m_total;
	double  m_min_ticks_per_pixel;
	float   m_width;
	double  m_ticks_per_pixel;
	double  m_start;
	bool    m_fitted;
};

namespace timeline_zoom {

// How many "notches" of zoom a wheel movement of `wheel` is worth (the wheel counts a click as 1,
// though some mice and drivers report several at once when it is turned quickly). A click or less
// counts in full; the part of a movement past one click counts for FAST_WHEEL_WEIGHT of a notch
// each, so a quick flick zooms further than a click but not wildly further. Signed, like `wheel`.
const double FAST_WHEEL_WEIGHT = 0.4;
// How close to the start or the end of the timeline the cursor has to be, as a fraction of the strip's width, for zooming to be about that end.
const float NEAR_END_FRACTION = 0.025f;
double notches(double wheel);

}  // namespace timeline_zoom

namespace timeline_ruler {

// The spacing of the labelled marks, in seconds, for a view in which a pixel is `seconds_per_pixel`:
// the smallest of 0.01, 0.02, 0.05, 0.1 ... 1, 2, 5, 10, 15, 30, 60 ... seconds apart that leaves
// at least `min_pixels` between marks.
double step_seconds(double seconds_per_pixel, double min_pixels);

// "1:05" for a time on a mark when the marks are whole seconds apart or more, "1:05.5" (or two
// decimals) when they are closer, and with hours when there are any: "1:02:03".
std::string label(double seconds, double step_seconds);

}  // namespace timeline_ruler
