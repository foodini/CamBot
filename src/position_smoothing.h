#pragma once

// Smoothing for the GPS track, for drawing it. The telemetry logger writes its positions as 32-bit
// floats, so a latitude and longitude near here only come in steps of about 0.4 meters; a track
// drawn from them zoomed in far enough turns into a staircase. Averaging each position with its
// neighbors takes most of that out. This is only for drawing: nothing that works out a distance, a
// speed or a heading should use it.
//
// Plain arithmetic, so it can be tested.

#include <vector>

namespace position_smoothing {

// How many samples either side of one are averaged with it (at 10 samples a second, a fifth of a second).
const int HALF_WINDOW = 2;

// Whether a latitude and longitude are a real position: the logger writes exactly 0, 0 (or nonsense)
// when the GPS has no fix.
bool has_fix(double latitude_degrees, double longitude_degrees);

// `latitude` and `longitude` have one entry per sample, in order. Each sample with a fix becomes the
// weighted average (nearer neighbors count more) of the samples with a fix up to `half_window` either side
// of it. The window is narrowed, evenly on both sides, so it never reaches past either end of the
// recording: the first and last samples stay as they were, and a straight line stays exactly straight.
// Samples with no fix are copied as they are and left out of the averages.
void smooth(const std::vector<double>& latitude, const std::vector<double>& longitude, int half_window,
	std::vector<double>* smooth_latitude, std::vector<double>* smooth_longitude);

}  // namespace position_smoothing
