// How an edit becomes stretches of sound with fades at the cuts (audio_plan.h).
#include <math.h>
#include <stdio.h>

#include "audio_plan.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

using namespace audio_plan;

// 1000 ticks to a second keeps the arithmetic easy to read: 48 samples a tick.
static const double TICK = 0.001;

static void test_ticks_to_samples() {
	CHECK(ticks_to_samples(1000, TICK) == 48000);
	CHECK(ticks_to_samples(0, TICK) == 0);
	CHECK(ticks_to_samples(1, TICK) == 48);
	// Rounded to the nearest sample, not cut off.
	CHECK(ticks_to_samples(1, 1.0 / 30000.0) == 2);    // 1.6 samples
	CHECK(ticks_to_samples(1, 1.0 / 90000.0) == 1);    // 0.53 samples
}

static void test_samples_to_ticks() {
	CHECK(samples_to_ticks(48000, TICK) == 1000);
	CHECK(samples_to_ticks(0, TICK) == 0);
	CHECK(samples_to_ticks(24, TICK) == 1);   // Half a tick rounds up.
	CHECK(samples_to_ticks(23, TICK) == 0);
	// Back and forth lands within a tick of where it began (a sample is almost two ticks at 90 kHz, so no better is possible).
	for (int64_t ticks = 0; ticks < 200000; ticks += 997) {
		const int64_t back = samples_to_ticks(ticks_to_samples(ticks, 1.0 / 90000.0), 1.0 / 90000.0);
		CHECK(back - ticks <= 1 && ticks - back <= 1);
	}
}

static void test_one_clip() {
	std::vector<Clip> clips;
	clips.push_back(Clip(2, 500, 1500));
	EditList list;
	list.set(clips);
	std::vector<Segment> segments;
	plan(list, TICK, &segments);
	CHECK(segments.size() == 1);
	CHECK(segments[0].media == 2);
	CHECK(segments[0].timeline_start == 0);
	CHECK(segments[0].length == 48000);
	CHECK(segments[0].source_start == 24000);
	// The ends of an edit fade as well: no click if it starts or stops mid-sound.
	CHECK(segments[0].fade_in && segments[0].fade_out);
}

static void test_joins() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));      // Then the same file straight on (a split): no fade between.
	clips.push_back(Clip(0, 1000, 2000));
	clips.push_back(Clip(0, 3000, 3500));   // A gap in the file: a cut.
	clips.push_back(Clip(1, 3500, 4000));   // Another file, though the numbers line up: a cut.
	clips.push_back(Clip(0, 0, 500));       // Back to an earlier part: a cut.
	EditList list;
	list.set(clips);
	std::vector<Segment> segments;
	plan(list, TICK, &segments);
	CHECK(segments.size() == 5);
	CHECK(segments[0].fade_in && !segments[0].fade_out);
	CHECK(!segments[1].fade_in && segments[1].fade_out);
	CHECK(segments[2].fade_in && segments[2].fade_out);
	CHECK(segments[3].fade_in && segments[3].fade_out);
	CHECK(segments[4].fade_in && segments[4].fade_out);

	// They tile the timeline: each starts where the last ended, and the total is the edit's length.
	CHECK(segments[0].timeline_start == 0);
	for (int i = 1; i < 5; i++)
		CHECK(segments[i].timeline_start == segments[i - 1].timeline_end());
	CHECK(segments[4].timeline_end() == ticks_to_samples(list.total_ticks(), TICK));
}

static void test_tiling_survives_rounding() {
	// A frame time that doesn't come to whole samples: ends still meet starts.
	const double tick = 1.0 / 90000.0;
	std::vector<Clip> clips;
	for (int i = 0; i < 20; i++)
		clips.push_back(Clip(0, 3003 * (i * 7), 3003 * (i * 7 + 3)));
	EditList list;
	list.set(clips);
	std::vector<Segment> segments;
	plan(list, tick, &segments);
	CHECK(segments.size() == 20);
	for (size_t i = 1; i < segments.size(); i++)
		CHECK(segments[i].timeline_start == segments[i - 1].timeline_end());
	CHECK(segments.back().timeline_end() == ticks_to_samples(list.total_ticks(), tick));
}

static void test_fade_gain() {
	Segment segment;
	segment.length = 10000;
	segment.fade_in = true;
	segment.fade_out = true;
	CHECK(segment.timeline_end() == 10000);
	CHECK(fade_gain(segment, 0) == 0.0f);
	CHECK(fade_gain(segment, FADE_SAMPLES) == 1.0f);
	CHECK(fade_gain(segment, 5000) == 1.0f);
	CHECK(fade_gain(segment, 9999 - FADE_SAMPLES) == 1.0f || fade_gain(segment, 9999 - FADE_SAMPLES) > 0.999f);
	CHECK(fade_gain(segment, 9999) == 0.0f);
	// Rises smoothly: never decreasing over the fade-in, halfway at the middle.
	float last = -1.0f;
	for (int i = 0; i <= FADE_SAMPLES; i++) {
		const float gain = fade_gain(segment, i);
		CHECK(gain >= last);
		last = gain;
	}
	CHECK(fabsf(fade_gain(segment, FADE_SAMPLES / 2) - 0.5f) < 0.001f);
	// The fall is the rise backward.
	for (int i = 0; i <= FADE_SAMPLES; i++)
		CHECK(fabsf(fade_gain(segment, i) - fade_gain(segment, 9999 - i)) < 1e-6f);

	// Without fades the volume is untouched.
	segment.fade_in = false;
	segment.fade_out = false;
	CHECK(fade_gain(segment, 0) == 1.0f);
	CHECK(fade_gain(segment, 9999) == 1.0f);
	segment.fade_in = true;
	CHECK(fade_gain(segment, 0) == 0.0f);
	CHECK(fade_gain(segment, 9999) == 1.0f);

	// A segment shorter than two fades gets shorter ones, and never goes above 1 or below 0.
	Segment tiny;
	tiny.length = 100;
	for (int i = 0; i < 100; i++) {
		const float gain = fade_gain(tiny, i);
		CHECK(gain >= 0.0f && gain <= 1.0f);
	}
	CHECK(fade_gain(tiny, 0) == 0.0f);
	CHECK(fade_gain(tiny, 99) == 0.0f);
	Segment nothing;
	nothing.length = 1;
	CHECK(fade_gain(nothing, 0) == 1.0f);
}

static void test_segment_at() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 0, 500));
	EditList list;
	list.set(clips);
	std::vector<Segment> segments;
	plan(list, TICK, &segments);
	CHECK(segment_at(segments, -1) == -1);
	CHECK(segment_at(segments, 0) == 0);
	CHECK(segment_at(segments, 47999) == 0);
	CHECK(segment_at(segments, 48000) == 1);
	CHECK(segment_at(segments, 71999) == 1);
	CHECK(segment_at(segments, 72000) == -1);
	std::vector<Segment> none;
	CHECK(segment_at(none, 0) == -1);
}

static void test_crossfade() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 0, 1000));
	clips.push_back(Clip(0, 1000, 2000));
	std::vector<Transition> transitions(4);
	transitions[1] = Transition(TRANSITION_CROSSFADE, 200);       // B starts at 800.
	EditList list;
	list.set(clips, transitions);
	std::vector<Segment> segments;
	plan(list, TICK, &segments);
	CHECK(segments.size() == 3);
	CHECK(segments[0].timeline_start == 0 && segments[0].length == 48000);
	CHECK(segments[1].timeline_start == 800 * 48 && segments[1].length == 48000);
	CHECK(segments[1].timeline_end() == 1800 * 48);
	// A cut after the crossfade: C begins where B ends.
	CHECK(segments[2].timeline_start == segments[1].timeline_end());
	CHECK(segments[2].timeline_end() == ticks_to_samples(list.total_ticks(), TICK));
	// The crossfade is the same length for both, and replaces their cut fades.
	CHECK(segments[0].xfade_out == 9600 && segments[1].xfade_in == 9600);
	CHECK(segments[0].xfade_in == 0 && segments[1].xfade_out == 0 && segments[2].xfade_in == 0);
	CHECK(segments[0].fade_in && !segments[0].fade_out);
	CHECK(!segments[1].fade_in && segments[1].fade_out);
	CHECK(segments[2].fade_in && segments[2].fade_out);

	// Equal power: where the two overlap, their volumes' squares add up to 1, from all A to all B.
	CHECK(crossfade_gain(segments[1], 0) == 0.0f);
	CHECK(fabsf(crossfade_gain(segments[1], 9600) - 1.0f) < 1e-6f);
	CHECK(fabsf(crossfade_gain(segments[1], 4800) - 0.70710678f) < 1e-5f);
	CHECK(crossfade_gain(segments[1], 30000) == 1.0f);
	const int64_t a_out = segments[0].length - segments[0].xfade_out;
	CHECK(crossfade_gain(segments[0], a_out - 1) == 1.0f);
	CHECK(fabsf(crossfade_gain(segments[0], a_out + 4800) - 0.70710678f) < 1e-5f);
	CHECK(fabsf(crossfade_gain(segments[0], segments[0].length - 1)) < 1e-3f);
	for (int64_t s = segments[1].timeline_start; s < segments[0].timeline_end(); s += 311) {
		const float a = crossfade_gain(segments[0], s - segments[0].timeline_start);
		const float b = crossfade_gain(segments[1], s - segments[1].timeline_start);
		CHECK(fabsf(a * a + b * b - 1.0f) < 1e-4f);
	}

	// Which segments hold a sample.
	int first = 0;
	int second = 0;
	CHECK(covering(segments, 20000, &first, &second) == 1 && first == 0 && second == -1);
	CHECK(covering(segments, 38400, &first, &second) == 2 && first == 0 && second == 1);
	CHECK(covering(segments, 47999, &first, &second) == 2 && first == 0 && second == 1);
	CHECK(covering(segments, 48000, &first, &second) == 1 && first == 1 && second == -1);
	CHECK(covering(segments, 100000, &first, &second) == 1 && first == 2);
	CHECK(covering(segments, segments[2].timeline_end(), &first, &second) == 0 && first == -1);
	CHECK(covering(segments, -1, &first, &second) == 0);

	// A crossfade between a split's two halves is a crossfade all the same (there is a cut to hide, with overlap).
	std::vector<Clip> halves;
	halves.push_back(Clip(0, 0, 1000));
	halves.push_back(Clip(0, 1000, 2000));
	std::vector<Transition> mixed(3);
	mixed[1] = Transition(TRANSITION_CROSSFADE, 100);
	list.set(halves, mixed);
	plan(list, TICK, &segments);
	CHECK(segments[0].xfade_out == 4800 && segments[1].xfade_in == 4800 && !segments[0].fade_out && !segments[1].fade_in);
	// And with the transition a cut it is a plain join with nothing between (no fade either).
	list.set(halves);
	plan(list, TICK, &segments);
	CHECK(segments[0].xfade_out == 0 && !segments[0].fade_out && !segments[1].fade_in);
}

int main() {
	test_ticks_to_samples();
	test_samples_to_ticks();
	test_one_clip();
	test_joins();
	test_tiling_survives_rounding();
	test_fade_gain();
	test_segment_at();
	test_crossfade();
	if (g_failures == 0)
		printf("audio_plan_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
