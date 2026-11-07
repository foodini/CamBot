#include "timeline_scale.h"

#include <math.h>
#include <stdio.h>

TimelineScale::TimelineScale() : m_total(0), m_min_ticks_per_pixel(1.0), m_width(0.0f), m_ticks_per_pixel(1.0), m_start(0.0), m_fitted(true) {
}

double TimelineScale::fit_ticks_per_pixel() const {
	if (m_width <= 0.0f || m_total <= 0)
		return m_min_ticks_per_pixel;
	double fit = (double)m_total / (double)m_width;
	// A timeline too short to be worth zooming is just shown as wide as it can go.
	return fit < m_min_ticks_per_pixel ? m_min_ticks_per_pixel : fit;
}

void TimelineScale::clamp_view() {
	double fit = fit_ticks_per_pixel();
	if (m_ticks_per_pixel > fit)
		m_ticks_per_pixel = fit;
	if (m_ticks_per_pixel < m_min_ticks_per_pixel)
		m_ticks_per_pixel = m_min_ticks_per_pixel;
	double visible = (double)m_width * m_ticks_per_pixel;
	double last_start = (double)m_total - visible;
	if (last_start < 0.0)
		last_start = 0.0;
	if (m_start > last_start)
		m_start = last_start;
	if (m_start < 0.0)
		m_start = 0.0;
}

void TimelineScale::set_extent(int64_t total_ticks, double min_ticks_per_pixel) {
	m_total = total_ticks < 0 ? 0 : total_ticks;
	m_min_ticks_per_pixel = min_ticks_per_pixel > 0.0 ? min_ticks_per_pixel : 1.0;
	if (m_fitted)
		fit();
	else
		clamp_view();
}

void TimelineScale::set_width(float pixels) {
	m_width = pixels < 0.0f ? 0.0f : pixels;
	if (m_fitted)
		fit();
	else
		clamp_view();
}

void TimelineScale::fit() {
	m_ticks_per_pixel = fit_ticks_per_pixel();
	m_start = 0.0;
	m_fitted = true;
}

int64_t TimelineScale::x_to_timeline_ticks(float x) const {
	double ticks = x_to_ticks(x);
	if (ticks < 0.0)
		ticks = 0.0;
	if (ticks > (double)m_total)
		ticks = (double)m_total;
	return (int64_t)llround(ticks);
}

void TimelineScale::zoom_at(float x, double factor) {
	if (factor <= 0.0 || m_width <= 0.0f)
		return;
	double anchor = x_to_ticks(x);
	double fit = fit_ticks_per_pixel();
	double wanted = m_ticks_per_pixel / factor;
	if (wanted > fit)
		wanted = fit;
	if (wanted < m_min_ticks_per_pixel)
		wanted = m_min_ticks_per_pixel;
	m_ticks_per_pixel = wanted;
	m_start = anchor - (double)x * m_ticks_per_pixel;
	clamp_view();
	m_fitted = m_ticks_per_pixel >= fit * (1.0 - 1e-9);
}

float TimelineScale::zoom_anchor(float x) const {
	const float reach = timeline_zoom::NEAR_END_FRACTION * m_width;
	const float start_x = tick_to_x(0.0);
	const float end_x = tick_to_x((double)m_total);
	const float to_start = fabsf(x - start_x);
	const float to_end = fabsf(x - end_x);
	if (to_start <= reach && to_start <= to_end)
		return start_x;
	if (to_end <= reach)
		return end_x;
	return x;
}

void TimelineScale::pan_by_pixels(float pixels) {
	m_start += (double)pixels * m_ticks_per_pixel;
	clamp_view();
}

bool TimelineScale::reveal(double ticks) {
	if (ticks >= m_start && ticks <= view_end())
		return false;
	m_start = ticks - 0.25 * (double)m_width * m_ticks_per_pixel;
	clamp_view();
	return true;
}

namespace timeline_zoom {

double notches(double wheel) {
	const double size = fabs(wheel);
	const double counted = size <= 1.0 ? size : 1.0 + FAST_WHEEL_WEIGHT * (size - 1.0);
	return wheel < 0.0 ? -counted : counted;
}

}  // namespace timeline_zoom

namespace timeline_ruler {

double step_seconds(double seconds_per_pixel, double min_pixels) {
	static const double STEPS[] = {
		0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1800.0, 3600.0, 7200.0, 14400.0
	};
	const int count = (int)(sizeof(STEPS) / sizeof(STEPS[0]));
	if (seconds_per_pixel <= 0.0)
		return STEPS[0];
	for (int i = 0; i < count; i++) {
		if (STEPS[i] / seconds_per_pixel >= min_pixels)
			return STEPS[i];
	}
	return STEPS[count - 1];
}

std::string label(double seconds, double step) {
	if (seconds < 0.0)
		seconds = 0.0;
	// Nudged up a hair so a mark at 1.0 s, worked out as 0.99999..., doesn't read 0:00.9.
	seconds += 1e-9;
	const int64_t whole = (int64_t)floor(seconds);
	const int hours = (int)(whole / 3600);
	const int minutes = (int)((whole / 60) % 60);
	const double rest = seconds - (double)(whole - whole % 60);   // Seconds within the minute, with the fraction.
	char text[64];
	if (step >= 1.0) {
		const int secs = (int)(whole % 60);
		if (hours > 0)
			snprintf(text, sizeof(text), "%d:%02d:%02d", hours, minutes, secs);
		else
			snprintf(text, sizeof(text), "%d:%02d", minutes, secs);
	} else {
		const int decimals = step < 0.1 ? 2 : 1;
		const int field = decimals + 3;   // "05.5" or "05.55"
		if (hours > 0)
			snprintf(text, sizeof(text), "%d:%02d:%0*.*f", hours, minutes, field, decimals, rest);
		else
			snprintf(text, sizeof(text), "%d:%0*.*f", minutes, field, decimals, rest);
	}
	return std::string(text);
}

}  // namespace timeline_ruler
