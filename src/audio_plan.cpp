#include "audio_plan.h"

#include <math.h>

namespace audio_plan {

int64_t ticks_to_samples(int64_t ticks, double seconds_per_tick) {
	return (int64_t)llround((double)ticks * seconds_per_tick * (double)SAMPLE_RATE);
}

int64_t samples_to_ticks(int64_t samples, double seconds_per_tick) {
	return (int64_t)llround((double)samples / (double)SAMPLE_RATE / seconds_per_tick);
}

void plan(const EditList& clips, double seconds_per_tick, std::vector<Segment>* segments) {
	segments->clear();
	const int count = clips.count();
	for (int i = 0; i < count; i++) {
		const Clip& clip = clips.clip(i);
		Segment segment;
		segment.media = clip.media;
		// Each begins at the sample its start time rounds to, and ends where the next one begins, so there is never a gap or
		// an overlap from rounding.
		segment.timeline_start = ticks_to_samples(clips.start(i), seconds_per_tick);
		const int64_t end = (i + 1 < count) ? ticks_to_samples(clips.start(i + 1), seconds_per_tick)
		                                    : ticks_to_samples(clips.total_ticks(), seconds_per_tick);
		segment.length = end - segment.timeline_start;
		segment.source_start = ticks_to_samples(clip.in_ticks, seconds_per_tick);
		const bool continues_previous = i > 0 && clips.clip(i - 1).media == clip.media && clips.clip(i - 1).out_ticks == clip.in_ticks;
		segment.fade_in = !continues_previous;
		segments->push_back(segment);
		if (i > 0)
			(*segments)[i - 1].fade_out = !continues_previous;
	}
}

float fade_gain(const Segment& segment, int64_t offset) {
	// Never more than half the segment, so the rise and the fall can't overlap.
	int64_t ramp = FADE_SAMPLES;
	if (ramp > segment.length / 2)
		ramp = segment.length / 2;
	if (ramp <= 0)
		return 1.0f;
	const double PI = 3.14159265358979323846;
	float gain = 1.0f;
	if (segment.fade_in && offset < ramp) {
		const double x = (double)offset / (double)ramp;
		gain *= (float)(0.5 - 0.5 * cos(PI * x));
	}
	const int64_t from_end = segment.length - offset;   // 1 at the last sample.
	if (segment.fade_out && from_end <= ramp) {
		const double x = (double)(from_end - 1) / (double)ramp;
		gain *= (float)(0.5 - 0.5 * cos(PI * x));
	}
	return gain;
}

int segment_at(const std::vector<Segment>& segments, int64_t sample) {
	// Binary search on the starts; the segments are in order and tile the timeline.
	int low = 0;
	int high = (int)segments.size() - 1;
	while (low <= high) {
		const int middle = (low + high) / 2;
		if (sample < segments[middle].timeline_start)
			high = middle - 1;
		else if (sample >= segments[middle].timeline_end())
			low = middle + 1;
		else
			return middle;
	}
	return -1;
}

}  // namespace audio_plan
