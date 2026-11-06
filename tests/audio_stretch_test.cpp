// Playing sound at other speeds (audio_stretch.h), against the fake audio file reader: "a.mp4" is a ramp (the left channel is the
// sample number), "tone.mp4" a steady 1013 Hz sine, both 100000 samples long.
#include <math.h>
#include <stdio.h>
#include <vector>

#include "audio_file_reader.h"
#include "audio_stretch.h"

void fake_audio_reset();
int  fake_audio_seek_count();

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { printf("FAIL line %d: %s -- ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

using namespace audio_stretch;

static const double TONE_HZ = 1013.0;
static const double RATE = 48000.0;

// The frequency of a sine in the left channel of `out` from frame `from` on, by counting upward zero crossings.
static double measured_hz(const std::vector<float>& out, int from, int frames) {
	int crossings = 0;
	int first = -1;
	int last = -1;
	for (int i = from + 1; i < from + frames; i++) {
		if (out[(size_t)(i - 1) * 2] < 0.0f && out[(size_t)i * 2] >= 0.0f) {
			crossings++;
			if (first < 0)
				first = i;
			last = i;
		}
	}
	if (crossings < 3)
		return 0.0;
	return (double)(crossings - 1) * RATE / (double)(last - first);
}

static double rms(const std::vector<float>& out, int from, int frames) {
	double total = 0.0;
	for (int i = from; i < from + frames; i++)
		total += (double)out[(size_t)i * 2] * out[(size_t)i * 2];
	return sqrt(total / (double)frames);
}

static float largest_step(const std::vector<float>& out, int from, int frames) {
	float biggest = 0.0f;
	for (int i = from + 1; i < from + frames; i++) {
		const float step = fabsf(out[(size_t)i * 2] - out[(size_t)(i - 1) * 2]);
		if (step > biggest)
			biggest = step;
	}
	return biggest;
}

static void test_source_window() {
	fake_audio_reset();
	AudioFileReader reader;
	std::string error;
	CHECK(reader.open("a.mp4", &error));
	SourceWindow window;
	window.set_reader(&reader);
	std::vector<float> out(3000 * 2);

	// Before the start is silence, then the sound.
	window.get(-100, 300, out.data());
	CHECK(out[0] == 0.0f && out[99 * 2] == 0.0f && out[100 * 2] == 0.0f && out[101 * 2] == 1.0f && out[299 * 2] == 199.0f);
	CHECK(out[101 * 2 + 1] == 1.0f);        // The other channel is the file's tag.
	// Reading the same stretch again, or a little behind, does not go back to the file.
	const int seeks = fake_audio_seek_count();
	window.get(50, 200, out.data());
	CHECK(out[0] == 50.0f && out[199 * 2] == 249.0f);
	window.get(100, 100, out.data());
	CHECK(out[0] == 100.0f);
	CHECK(fake_audio_seek_count() == seeks);
	// Forward in steps.
	for (int start = 300; start < 20000; start += 700) {
		window.get(start, 1500, out.data());
		CHECK(out[0] == (float)start && out[1499 * 2] == (float)(start + 1499));
	}
	// A long jump is a seek, and a jump back still gives the right sound.
	window.get(90000, 1000, out.data());
	CHECK(out[0] == 90000.0f && out[999 * 2] == 90999.0f);
	window.get(5, 10, out.data());
	CHECK(out[0] == 5.0f && out[9 * 2] == 14.0f);
	// Past the end is silence.
	window.get(99990, 30, out.data());
	CHECK(out[9 * 2] == 99999.0f && out[10 * 2] == 0.0f && out[29 * 2] == 0.0f);
	// No reader: silence.
	SourceWindow nothing;
	out[0] = 5.0f;
	nothing.get(0, 10, out.data());
	CHECK(out[0] == 0.0f);
}

static void test_resample_ramp() {
	AudioFileReader reader;
	std::string error;
	CHECK(reader.open("a.mp4", &error));
	SourceWindow window;
	window.set_reader(&reader);
	std::vector<float> out(5000 * 2);
	// Normal speed is a copy.
	resample(&window, 1000, 1.0, 0, 2000, out.data());
	for (int n = 0; n < 2000; n += 97)
		CHECK(fabsf(out[(size_t)n * 2] - (1000.0f + (float)n)) < 1e-3f);
	// Half speed: a ramp is a straight line, so each output sample is half a sample further on.
	resample(&window, 1000, 0.5, 0, 3000, out.data());
	for (int n = 0; n < 3000; n += 97)
		CHECK_MSG(fabsf(out[(size_t)n * 2] - (1000.0f + 0.5f * (float)n)) < 1e-2f, "n=%d got %f", n, out[(size_t)n * 2]);
	// Double and sixteen times: the average over the stretch each covers is its middle.
	resample(&window, 1000, 2.0, 0, 3000, out.data());
	for (int n = 0; n < 3000; n += 97)
		CHECK_MSG(fabsf(out[(size_t)n * 2] - (1000.0f + 2.0f * (float)n)) < 0.05f, "n=%d got %f", n, out[(size_t)n * 2]);
	resample(&window, 2000, 16.0, 0, 5000, out.data());
	for (int n = 0; n < 5000; n += 97)
		CHECK_MSG(fabsf(out[(size_t)n * 2] - (2000.0f + 16.0f * (float)n)) < 0.2f, "n=%d got %f", n, out[(size_t)n * 2]);
	// In pieces gives the same as in one.
	std::vector<float> whole(4000 * 2);
	std::vector<float> pieces(4000 * 2);
	resample(&window, 777, 1.7, 100, 4000, whole.data());
	int done = 0;
	const int sizes[] = { 1, 333, 1500, 7, 2159 };
	for (int i = 0; i < 5; i++) {
		resample(&window, 777, 1.7, 100 + done, sizes[i], pieces.data() + (size_t)done * 2);
		done += sizes[i];
	}
	CHECK(done == 4000);
	for (size_t i = 0; i < whole.size(); i++)
		CHECK(whole[i] == pieces[i]);
}

static void test_resample_changes_pitch() {
	AudioFileReader reader;
	std::string error;
	CHECK(reader.open("tone.mp4", &error));
	SourceWindow window;
	window.set_reader(&reader);
	const double speeds[] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 };
	for (size_t k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
		const int frames = 10000;
		std::vector<float> out((size_t)frames * 2);
		resample(&window, 2000, speeds[k], 0, frames, out.data());
		const double hz = measured_hz(out, 0, frames);
		CHECK_MSG(fabs(hz - TONE_HZ * speeds[k]) < 0.02 * TONE_HZ * speeds[k], "speed %g: %.1f Hz, wanted %.1f", speeds[k], hz, TONE_HZ * speeds[k]);
	}
}

static void test_keep_pitch_tone() {
	AudioFileReader reader;
	std::string error;
	CHECK(reader.open("tone.mp4", &error));
	SourceWindow window;
	window.set_reader(&reader);
	const double speeds[] = { 0.25, 0.5, 0.8, 1.3, 2.0, 3.0, 4.0 };
	for (size_t k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
		PitchKeeper keeper;
		keeper.start(&window, 3000, speeds[k], 0);
		const int frames = 20000;
		std::vector<float> out((size_t)frames * 2);
		keeper.render(out.data(), frames);
		CHECK(keeper.next_output() == frames);
		const double hz = measured_hz(out, 2000, frames - 2000);
		CHECK_MSG(fabs(hz - TONE_HZ) < 0.02 * TONE_HZ, "speed %g: %.1f Hz, wanted %.1f", speeds[k], hz, TONE_HZ);
		const double level = rms(out, 2000, frames - 2000);
		CHECK_MSG(fabs(level - 0.7071) < 0.1, "speed %g: rms %.3f", speeds[k], level);
		// No clicks: a pure tone only ever moves a little from one sample to the next (at most 0.133 here).
		const float step = largest_step(out, 0, frames);
		CHECK_MSG(step < 0.25f, "speed %g: biggest step %.3f", speeds[k], step);
	}
}

static void test_keep_pitch_timing_and_restart() {
	AudioFileReader ramp_reader;
	std::string error;
	CHECK(ramp_reader.open("a.mp4", &error));
	SourceWindow ramp;
	ramp.set_reader(&ramp_reader);
	// The sound comes from about where it should: output frame n from about start + n * speed. Within a grain the sound runs at
	// its own speed, so a ramp strays from the line by up to about a grain's worth of the difference, plus the search.
	const double speeds[] = { 0.5, 1.5, 4.0 };
	for (size_t k = 0; k < 3; k++) {
		PitchKeeper keeper;
		keeper.start(&ramp, 1000, speeds[k], 0);
		std::vector<float> out(20000 * 2);
		keeper.render(out.data(), 20000);
		for (int n = 3000; n < 19000; n += 1000) {
			const double want = 1000.0 + (double)n * speeds[k];
			const double bound = 600.0 + 1600.0 * fmax(1.0, fabs(speeds[k] - 1.0));
			CHECK_MSG(fabs((double)out[(size_t)n * 2] - want) < bound, "speed %g n=%d: %.0f vs %.0f", speeds[k], n, out[(size_t)n * 2], want);
		}
	}

	AudioFileReader reader;
	CHECK(reader.open("tone.mp4", &error));
	SourceWindow window;
	window.set_reader(&reader);
	// In odd-sized pieces it is the same as in one go.
	PitchKeeper one;
	one.start(&window, 500, 0.7, 0);
	std::vector<float> whole(9000 * 2);
	one.render(whole.data(), 9000);
	PitchKeeper pieces;
	pieces.start(&window, 500, 0.7, 0);
	std::vector<float> joined(9000 * 2);
	int done = 0;
	const int sizes[] = { 1, 100, 767, 768, 769, 1500, 33, 2000, 3062 };
	for (int i = 0; i < 9; i++) {
		pieces.render(joined.data() + (size_t)done * 2, sizes[i]);
		done += sizes[i];
	}
	CHECK(done == 9000);
	for (size_t i = 0; i < whole.size(); i++) {
		if (whole[i] != joined[i]) {
			CHECK_MSG(false, "differs at %zu", i);
			break;
		}
	}
	// Starting part way in (a jump in the timeline) gives a good tone straight away, with no click.
	PitchKeeper restarted;
	restarted.start(&window, 500, 0.7, 12345);
	CHECK(restarted.next_output() == 12345);
	std::vector<float> out(8000 * 2);
	restarted.render(out.data(), 8000);
	CHECK(restarted.next_output() == 12345 + 8000);
	const double hz = measured_hz(out, 0, 8000);
	CHECK_MSG(fabs(hz - TONE_HZ) < 0.02 * TONE_HZ, "restarted: %.1f Hz", hz);
	CHECK(largest_step(out, 0, 8000) < 0.25f);
	CHECK(fabs(rms(out, 0, 8000) - 0.7071) < 0.1);
	// The first frames are at full strength (no fade in from the start of a stretch).
	PitchKeeper beginning;
	beginning.start(&window, 500, 2.0, 0);
	beginning.render(out.data(), 2000);
	CHECK(rms(out, 0, 400) > 0.5);
	// Nothing to read from: silence.
	SourceWindow nothing;
	PitchKeeper quiet;
	quiet.start(&nothing, 0, 0.5, 0);
	quiet.render(out.data(), 5000);
	CHECK(rms(out, 0, 5000) == 0.0);
}

int main() {
	test_source_window();
	test_resample_ramp();
	test_resample_changes_pitch();
	test_keep_pitch_tone();
	test_keep_pitch_timing_and_restart();
	if (g_failures == 0)
		printf("audio_stretch_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
