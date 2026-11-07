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
		// Each begins at the sample its start time rounds to, and ends at the sample its end time rounds to; where one clip follows
		// another with a cut those are the same sample, so there is never a gap or an overlap from rounding.
		segment.timeline_start = ticks_to_samples(clips.start(i), seconds_per_tick);
		const int64_t end = ticks_to_samples(clips.end(i), seconds_per_tick);
		segment.length = end - segment.timeline_start;
		segment.source_start = ticks_to_samples(clip.in_ticks, seconds_per_tick);
		segment.speed = clip.speed;
		segment.mode = clip.audio_mode;
		bool crossfaded = false;
		if (i > 0 && clips.overlap_ticks(i) > 0) {
			// The two clips overlap by the same number of samples, counted from the end of the earlier to the start of the later.
			int64_t overlap = ticks_to_samples(clips.end(i - 1), seconds_per_tick) - segment.timeline_start;
			if (overlap > segment.length / 2)
				overlap = segment.length / 2;
			if (overlap > (*segments)[i - 1].length / 2)
				overlap = (*segments)[i - 1].length / 2;
			if (overlap > 0) {
				crossfaded = true;
				segment.xfade_in = overlap;
				(*segments)[i - 1].xfade_out = overlap;
			}
		}
		bool continues_previous = !crossfaded && i > 0 && clips.clip(i - 1).media == clip.media && clips.clip(i - 1).out_ticks == clip.in_ticks;
		if (continues_previous) {
			const Clip& before = clips.clip(i - 1);
			const bool same_treatment = before.speed == clip.speed && (clip.speed == 1.0 || before.audio_mode == clip.audio_mode);
			const bool stretched_afresh = clip.speed != 1.0 && clip.audio_mode == AUDIO_KEEP_PITCH;
			continues_previous = same_treatment && !stretched_afresh;
		}
		segment.fade_in = !continues_previous && !crossfaded;
		segments->push_back(segment);
		if (i > 0)
			(*segments)[i - 1].fade_out = !continues_previous && !crossfaded;
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

EdgeFades edge_fades(const EditList& clips, double seconds_per_tick) {
	EdgeFades fades;
	if (clips.count() == 0)
		return fades;
	const Transition start = clips.transition(0);
	const Transition end = clips.transition(clips.count());
	if (start.type == TRANSITION_FADE && start.ticks > 0)
		fades.in_samples = ticks_to_samples(start.ticks, seconds_per_tick);
	if (end.type == TRANSITION_FADE && end.ticks > 0)
		fades.out_samples = ticks_to_samples(end.ticks, seconds_per_tick);
	return fades;
}

float edge_gain(const EdgeFades& fades, int64_t total_samples, int64_t sample) {
	double gain = 1.0;
	if (fades.in_samples > 0 && sample < fades.in_samples)
		gain = (double)(sample > 0 ? sample : 0) / (double)fades.in_samples;
	if (fades.out_samples > 0 && sample > total_samples - fades.out_samples) {
		const double down = (double)(total_samples > sample ? total_samples - sample : 0) / (double)fades.out_samples;
		if (down < gain)
			gain = down;
	}
	return (float)gain;
}

float crossfade_gain(const Segment& segment, int64_t offset) {
	const double HALF_PI = 1.57079632679489661923;
	double gain = 1.0;
	if (segment.xfade_in > 0 && offset < segment.xfade_in) {
		const double x = (double)(offset > 0 ? offset : 0) / (double)segment.xfade_in;
		gain *= sin(HALF_PI * x);
	}
	const int64_t into_out = offset - (segment.length - segment.xfade_out);
	if (segment.xfade_out > 0 && into_out > 0) {
		const double x = (double)into_out / (double)segment.xfade_out;
		gain *= cos(HALF_PI * (x < 1.0 ? x : 1.0));
	}
	return (float)gain;
}

int covering(const std::vector<Segment>& segments, int64_t sample, int* first, int* second) {
	*first = -1;
	*second = -1;
	const int index = segment_at(segments, sample);
	if (index < 0)
		return 0;
	int low = index;
	int high = index;
	if (index > 0 && sample >= segments[index - 1].timeline_start && sample < segments[index - 1].timeline_end())
		low = index - 1;
	if (index + 1 < (int)segments.size() && sample >= segments[index + 1].timeline_start && sample < segments[index + 1].timeline_end())
		high = index + 1;
	*first = low;
	if (high != low) {
		*second = high;
		return 2;
	}
	return 1;
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
