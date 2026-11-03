#include "timecode.h"

#include <math.h>
#include <stdio.h>

namespace timecode {

Timecode for_ticks(int64_t ticks, int tick_num, int tick_den, int64_t frame_ticks) {
	Timecode result;
	if (ticks < 0)
		ticks = 0;
	if (tick_num <= 0 || tick_den <= 0 || frame_ticks <= 0)
		return result;

	const double seconds_per_tick = (double)tick_num / (double)tick_den;
	// The frame this time belongs to, counting from 0, and when that frame begins.
	const int64_t frame_index = (int64_t)llround((double)ticks / (double)frame_ticks);
	const double frame_seconds = (double)frame_index * (double)frame_ticks * seconds_per_tick;
	const int64_t whole_seconds = (int64_t)floor(frame_seconds + 1e-6);

	// The first frame that begins at or after the start of that second.
	const double frames_per_second = 1.0 / ((double)frame_ticks * seconds_per_tick);
	int64_t first_frame = (int64_t)ceil((double)whole_seconds * frames_per_second - 1e-6);
	if (first_frame < 0)
		first_frame = 0;
	int64_t frame = frame_index - first_frame;
	if (frame < 0)
		frame = 0;

	result.hours = (int)(whole_seconds / 3600);
	result.minutes = (int)((whole_seconds / 60) % 60);
	result.seconds = (int)(whole_seconds % 60);
	result.frame = (int)frame;
	return result;
}

std::string format(const Timecode& time, int hours_width) {
	char text[64];
	snprintf(text, sizeof(text), "%*d:%02d:%02d#%d", hours_width, time.hours, time.minutes, time.seconds, time.frame);
	return std::string(text);
}

std::string format_frame_rate(int tick_num, int tick_den, int64_t frame_ticks) {
	if (tick_num <= 0 || tick_den <= 0 || frame_ticks <= 0)
		return "?";
	const double rate = (double)tick_den / ((double)tick_num * (double)frame_ticks);
	char text[32];
	if (fabs(rate - floor(rate + 0.5)) < 0.005)
		snprintf(text, sizeof(text), "%d", (int)floor(rate + 0.5));
	else
		snprintf(text, sizeof(text), "%.2f", rate);
	return std::string(text);
}

}  // namespace timecode
