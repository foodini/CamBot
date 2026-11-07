// Playing an edit's sound (audio_timeline.h), against a fake file reader whose samples say where they came from: the left channel
// is the sample number in the file, the right is the file's tag (fake_audio_file_reader.cpp).
#include <math.h>
#include <stdio.h>
#include <vector>

#include "audio_timeline.h"

void fake_audio_reset();
int  fake_audio_seek_count();
int  fake_audio_open_count();

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { printf("FAIL line %d: %s -- ", __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

using namespace audio_plan;

static const double TICK = 0.001;   // 48 samples to a tick.

static std::vector<std::string> names(const char* a, const char* b, const char* c) {
	std::vector<std::string> paths;
	paths.push_back(a);
	paths.push_back(b);
	paths.push_back(c);
	return paths;
}

static EditList make_list(const std::vector<Clip>& clips) {
	EditList list;
	list.set(clips);
	return list;
}

static void test_plays_clips_in_order() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 200));    // File a, from sample 4800: 4800 samples.
	clips.push_back(Clip(1, 0, 100));      // File b, from the start.
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	CHECK(timeline.total_samples() == 9600);

	std::vector<float> out(9600 * 2, -1.0f);
	timeline.render(out.data(), 9600);
	CHECK(timeline.position() == 9600);
	// The middle of the first clip: file a at 4800 + n, tag 1, full volume.
	CHECK(out[2000 * 2] == 4800.0f + 2000.0f && out[2000 * 2 + 1] == 1.0f);
	// And of the second: file b at n, tag 2.
	CHECK(out[(4800 + 2000) * 2] == 2000.0f && out[(4800 + 2000) * 2 + 1] == 2.0f);
	// Faded to nothing either side of the cut (and at the very ends).
	CHECK(out[0] == 0.0f && out[1] == 0.0f);
	CHECK(out[(4800 - 1) * 2] == 0.0f);
	CHECK(out[4800 * 2] == 0.0f);
	CHECK(out[(9600 - 1) * 2] == 0.0f);
	// Rising: the right channel (a steady tag) shows the gain directly.
	CHECK(out[(4800 + 100) * 2 + 1] > out[(4800 + 50) * 2 + 1]);
	CHECK(fabsf(out[(4800 + 240) * 2 + 1] - 1.0f) < 1.0f);   // Halfway up a fade to 2.0.
	CHECK(fabsf(out[(4800 + 480) * 2 + 1] - 2.0f) < 1e-6f);
}

static void test_split_carries_on_without_fade_or_seek() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 150));
	clips.push_back(Clip(0, 150, 300));   // A split: straight on.
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	std::vector<float> out((size_t)timeline.total_samples() * 2);
	timeline.render(out.data(), (int)timeline.total_samples());
	const int join = 50 * 48;
	// No dip at the join, and the left channel runs on unbroken from one clip into the next.
	for (int i = join - 4; i < join + 4; i++) {
		CHECK(out[i * 2 + 1] == 1.0f);
		CHECK(out[i * 2] == 4800.0f + (float)i);
	}
	// One seek for the start of the first clip and none for the second.
	CHECK(fake_audio_seek_count() == 1);
}

static void test_reordered_clips_seek() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 200, 300));
	clips.push_back(Clip(0, 0, 100));   // Back to an earlier part of the same file.
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	std::vector<float> out((size_t)timeline.total_samples() * 2);
	timeline.render(out.data(), (int)timeline.total_samples());
	CHECK(out[1000 * 2] == 9600.0f + 1000.0f);
	CHECK(out[(4800 + 1000) * 2] == 1000.0f);
	CHECK(fake_audio_seek_count() == 2);
	CHECK(out[4800 * 2 + 1] == 0.0f);   // Faded at the cut.
}

static void test_render_in_small_blocks_matches() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 150));
	clips.push_back(Clip(1, 0, 40));
	clips.push_back(Clip(0, 300, 330));
	AudioTimeline whole;
	whole.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	const int total = (int)whole.total_samples();
	std::vector<float> expected((size_t)total * 2);
	whole.render(expected.data(), total);

	AudioTimeline pieces;
	pieces.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	std::vector<float> got((size_t)total * 2);
	int at = 0;
	const int blocks[5] = { 1, 1000, 333, 4096, 7 };
	int which = 0;
	while (at < total) {
		int n = blocks[which++ % 5];
		if (n > total - at)
			n = total - at;
		pieces.render(got.data() + (size_t)at * 2, n);
		at += n;
	}
	bool same = true;
	for (size_t i = 0; i < got.size(); i++)
		same = same && got[i] == expected[i];
	CHECK(same);
}

static void test_seek_and_the_end() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	timeline.seek(2000);
	CHECK(timeline.position() == 2000);
	std::vector<float> out(2 * 8000, 7.0f);
	timeline.render(out.data(), 8000);   // Runs 2800 past the end.
	CHECK(out[0] == 2000.0f);
	CHECK(timeline.position() == 10000);
	CHECK(out[(4799 - 2000) * 2] == 0.0f);        // Last sample of the edit, faded out.
	CHECK(out[(4800 - 2000) * 2] == 0.0f && out[(4800 - 2000) * 2 + 1] == 0.0f);   // Then silence.
	CHECK(out[7999 * 2] == 0.0f);
	timeline.seek(-5);
	CHECK(timeline.position() == 0);
	timeline.seek(1 << 30);
	CHECK(timeline.position() == 4800);
}

// What the fades are for: cutting a steady tone into pieces and putting them back out of order must not make a click. A click is
// a jump between neighbouring samples much bigger than the tone itself ever makes (a 1 kHz sine at full level moves 0.13 at most
// from one sample to the next; a hard cut at the wrong phase jumps by up to 2).
static float biggest_step(const std::vector<float>& out, int total) {
	float biggest = 0.0f;
	for (int i = 1; i < total; i++) {
		const float step = fabsf(out[i * 2] - out[(i - 1) * 2]);
		if (step > biggest)
			biggest = step;
	}
	return biggest;
}

static void test_cuts_make_no_click() {
	std::vector<Clip> clips;
	// Ten pieces of a one-second stretch, from places that do not line up, in a shuffled order.
	const int order[10] = { 7, 2, 9, 0, 4, 8, 1, 6, 3, 5 };
	for (int i = 0; i < 10; i++)
		clips.push_back(Clip(0, order[i] * 77 + 5, order[i] * 77 + 5 + 61));
	AudioTimeline timeline;
	timeline.set_edit(names("tone.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	const int total = (int)timeline.total_samples();
	std::vector<float> out((size_t)total * 2);
	timeline.render(out.data(), total);
	CHECK(biggest_step(out, total) < 0.2f);

	// The same edit with every cut left raw (the fades taken out) does click, so the check above is a real one.
	float raw_biggest = 0.0f;
	for (int c = 1; c < 10; c++) {
		const int64_t before = audio_plan::ticks_to_samples(clips[c - 1].out_ticks - 1, TICK);
		const int64_t after = audio_plan::ticks_to_samples(clips[c].in_ticks, TICK);
		const float a = (float)sin(2.0 * 3.14159265358979 * 1013.0 * (double)before / 48000.0);
		const float b = (float)sin(2.0 * 3.14159265358979 * 1013.0 * (double)after / 48000.0);
		if (fabsf(a - b) > raw_biggest)
			raw_biggest = fabsf(a - b);
	}
	CHECK(raw_biggest > 0.5f);

	// A split carries on with no step at all.
	std::vector<Clip> split;
	split.push_back(Clip(0, 100, 150));
	split.push_back(Clip(0, 150, 400));
	AudioTimeline carried;
	carried.set_edit(names("tone.mp4", "b.mp4", "c.mp4"), make_list(split), TICK);
	const int carried_total = (int)carried.total_samples();
	std::vector<float> carried_out((size_t)carried_total * 2);
	carried.render(carried_out.data(), carried_total);
	CHECK(biggest_step(carried_out, carried_total) < 0.14f);
}

static void test_files_without_sound() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(1, 0, 100));    // No sound: silence, not an error.
	clips.push_back(Clip(0, 0, 100));
	clips.push_back(Clip(2, 0, 100));    // Won't open: silence, and the user is told.
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "silent.mp4", "missing.mp4"), make_list(clips), TICK);
	std::vector<float> out((size_t)timeline.total_samples() * 2, 5.0f);
	timeline.render(out.data(), (int)timeline.total_samples());
	CHECK(out[1000 * 2] == 0.0f && out[1000 * 2 + 1] == 0.0f);
	CHECK(out[(4800 + 1000) * 2 + 1] == 1.0f);
	CHECK(out[(9600 + 1000) * 2] == 0.0f);
	CHECK(timeline.last_error().find("missing") != std::string::npos);
	// Each is tried once, however often it comes up.
	timeline.seek(0);
	timeline.render(out.data(), (int)timeline.total_samples());
	CHECK(fake_audio_open_count() == 3);
}

static void test_editing_keeps_readers() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	std::vector<float> out(2 * 100);
	timeline.render(out.data(), 100);
	CHECK(fake_audio_open_count() == 1);
	// A different edit of the same files does not reopen anything.
	clips.push_back(Clip(0, 200, 300));
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(clips), TICK);
	CHECK(timeline.total_samples() == 9600);
	timeline.seek(0);
	timeline.render(out.data(), 100);
	CHECK(fake_audio_open_count() == 1);
	// Other files do.
	timeline.set_edit(names("c.mp4", "b.mp4", "a.mp4"), make_list(clips), TICK);
	timeline.seek(0);
	timeline.render(out.data(), 100);
	CHECK(fake_audio_open_count() == 2);
	// An empty edit is silent and harmless.
	std::vector<Clip> none;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), make_list(none), TICK);
	CHECK(timeline.total_samples() == 0);
	out.assign(20, 1.0f);
	timeline.render(out.data(), 10);
	CHECK(out[0] == 0.0f && out[19] == 0.0f);
}

static void test_clips_at_other_speeds() {
	fake_audio_reset();
	// TICK is 48 samples, so these clips are 4800 samples of the file (100 ticks) each.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));                                    // Normal: 4800 samples.
	clips.push_back(Clip(0, 100, 300, 2.0, AUDIO_CHANGE_PITCH));        // 200 ticks of file at double speed: 4800 samples.
	clips.push_back(Clip(0, 300, 400, 0.5, AUDIO_MUTE));                // Silent: 9600 samples.
	clips.push_back(Clip(2, 0, 200, 2.0, AUDIO_KEEP_PITCH));            // The tone, stretched: 4800 samples.
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "tone.mp4"), make_list(clips), TICK);
	CHECK(timeline.total_samples() == 4800 + 4800 + 9600 + 4800);
	std::vector<float> out((size_t)timeline.total_samples() * 2);
	timeline.render(out.data(), (int)timeline.total_samples());

	// Played like a tape: the left channel (the sample number) runs twice as fast, from where the clip begins in the file.
	const int second = 4800;
	for (int n = 1000; n < 3800; n += 311)
		CHECK_MSG(fabsf(out[(size_t)(second + n) * 2] - (4800.0f + 2.0f * (float)n)) < 0.1f, "n=%d got %f", n, out[(size_t)(second + n) * 2]);
	// Faded in and out at its ends, being a different treatment from the clip before.
	CHECK(out[(size_t)second * 2 + 1] == 0.0f && out[(size_t)(second - 1) * 2 + 1] == 0.0f);
	// Muted is silent all the way through.
	const int third = 9600;
	for (int n = 0; n < 9600; n += 97)
		CHECK(out[(size_t)(third + n) * 2] == 0.0f && out[(size_t)(third + n) * 2 + 1] == 0.0f);
	// Stretched without changing pitch: still a 1013 Hz tone, and no click where it begins or ends.
	const int fourth = 19200;
	int crossings = 0;
	int first_crossing = -1;
	int last_crossing = -1;
	for (int n = 1000; n < 3800; n++) {
		if (out[(size_t)(fourth + n - 1) * 2] < 0.0f && out[(size_t)(fourth + n) * 2] >= 0.0f) {
			crossings++;
			if (first_crossing < 0)
				first_crossing = n;
			last_crossing = n;
		}
	}
	const double hz = (double)(crossings - 1) * 48000.0 / (double)(last_crossing - first_crossing);
	CHECK_MSG(fabs(hz - 1013.0) < 25.0, "tone is %.1f Hz", hz);
	CHECK(out[(size_t)fourth * 2] == 0.0f && out[(size_t)(fourth + 4799) * 2] == 0.0f);

	// In small blocks it is the same as in one.
	AudioTimeline blocks;
	blocks.set_edit(names("a.mp4", "b.mp4", "tone.mp4"), make_list(clips), TICK);
	std::vector<float> small((size_t)timeline.total_samples() * 2);
	int done = 0;
	const int total = (int)timeline.total_samples();
	while (done < total) {
		const int count = total - done < 777 ? total - done : 777;
		blocks.render(small.data() + (size_t)done * 2, count);
		done += count;
	}
	for (size_t i = 0; i < out.size(); i++) {
		if (out[i] != small[i]) {
			CHECK_MSG(false, "blocks differ at %zu", i);
			break;
		}
	}
	// Seeking into the middle of a stretched clip gives the same sound as playing up to it.
	AudioTimeline jumped;
	jumped.set_edit(names("a.mp4", "b.mp4", "tone.mp4"), make_list(clips), TICK);
	jumped.seek(fourth + 2000);
	std::vector<float> tail(1500 * 2);
	jumped.render(tail.data(), 1500);
	float biggest_step = 0.0f;
	for (int i = 1; i < 1500; i++)
		biggest_step = fmaxf(biggest_step, fabsf(tail[(size_t)i * 2] - tail[(size_t)(i - 1) * 2]));
	CHECK(biggest_step < 0.25f);
}

static void test_fade_in_and_out_of_the_edit() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 400));      // 400 ticks: 19200 samples, all in file a, tag 1.
	std::vector<Transition> transitions(2);
	transitions[0] = Transition(TRANSITION_FADE, 100);     // 4800 samples up.
	transitions[1] = Transition(TRANSITION_FADE, 50);      // 2400 samples down.
	EditList list;
	list.set(clips, transitions);
	CHECK(list.transitions_valid());
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), list, TICK);
	const int total = (int)timeline.total_samples();
	CHECK(total == 19200);
	std::vector<float> out((size_t)total * 2);
	timeline.render(out.data(), total);
	// The right channel is the file's steady tag (1): its level is the gain. Silent at the very start, three quarters up three
	// quarters of the way, full from then until the fall begins, and half down half way, nothing at the end.
	CHECK(out[1] == 0.0f);
	CHECK(fabsf(out[2400 * 2 + 1] - 0.5f) < 1e-4f);
	CHECK(fabsf(out[3600 * 2 + 1] - 0.75f) < 1e-4f);
	CHECK(fabsf(out[5000 * 2 + 1] - 1.0f) < 1e-6f);
	CHECK(fabsf(out[10000 * 2 + 1] - 1.0f) < 1e-6f);
	CHECK(fabsf(out[(total - 1200) * 2 + 1] - 0.5f) < 1e-3f);
	CHECK(out[(total - 1) * 2 + 1] < 1e-3f);
	// In small blocks it is the same.
	timeline.seek(0);
	std::vector<float> blocks((size_t)total * 2);
	for (int at = 0; at < total; at += 1000)
		timeline.render(blocks.data() + (size_t)at * 2, total - at < 1000 ? total - at : 1000);
	bool same = true;
	for (size_t i = 0; i < blocks.size(); i++) {
		if (blocks[i] != out[i])
			same = false;
	}
	CHECK(same);
	// A list with no fades is as it was.
	EditList plain;
	plain.set(clips);
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), plain, TICK);
	timeline.seek(0);
	timeline.render(out.data(), total);
	CHECK(fabsf(out[2400 * 2 + 1] - 1.0f) < 1e-6f);
}

static void test_crossfade_mix() {
	fake_audio_reset();
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));      // File a, tag 1: 4800 samples.
	clips.push_back(Clip(1, 0, 100));      // File b, tag 2, starting 40 ticks before the first ends (at 60 ticks: sample 2880).
	std::vector<Transition> transitions(3);
	transitions[1] = Transition(TRANSITION_CROSSFADE, 40);
	EditList list;
	list.set(clips, transitions);
	CHECK(list.transitions_valid());
	AudioTimeline timeline;
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), list, TICK);
	const int total = (int)timeline.total_samples();
	CHECK(total == 160 * 48);
	std::vector<float> out((size_t)total * 2);
	timeline.render(out.data(), total);
	// The right channel is the tag times the gain: all of the first before the crossfade, all of the second after it, and in
	// it a sine and cosine mix (a quarter, half and three quarters of the way, an overlap of 1920 samples from 2880).
	CHECK(fabsf(out[1000 * 2 + 1] - 1.0f) < 1e-6f);
	CHECK(fabsf(out[5000 * 2 + 1] - 2.0f) < 1e-6f);
	CHECK(fabsf(out[2880 * 2 + 1] - 1.0f) < 1e-5f);
	CHECK(fabsf(out[(2880 + 960) * 2 + 1] - (0.70710678f + 2.0f * 0.70710678f)) < 1e-3f);
	CHECK(fabsf(out[(2880 + 480) * 2 + 1] - (cosf(0.39269908f) + 2.0f * sinf(0.39269908f))) < 1e-3f);
	CHECK(fabsf(out[(4800 - 1) * 2 + 1] - 2.0f) < 1e-2f);
	// In small blocks it is the same.
	timeline.seek(0);
	std::vector<float> blocks((size_t)total * 2);
	for (int at = 0; at < total; at += 700)
		timeline.render(blocks.data() + (size_t)at * 2, total - at < 700 ? total - at : 700);
	bool same = true;
	for (size_t i = 0; i < blocks.size(); i++) {
		if (fabsf(blocks[i] - out[i]) > 1e-5f)
			same = false;
	}
	CHECK(same);

	// Two clips from the same file, crossfaded: each carries on from where it was, without seeking back and forth every block.
	fake_audio_reset();
	std::vector<Clip> halves;
	halves.push_back(Clip(0, 0, 100));
	halves.push_back(Clip(0, 100, 200));
	list.set(halves, transitions);
	timeline.set_edit(names("a.mp4", "b.mp4", "c.mp4"), list, TICK);
	const int total_halves = (int)timeline.total_samples();
	std::vector<float> same_file((size_t)total_halves * 2);
	timeline.seek(0);
	for (int at = 0; at < total_halves; at += 100)
		timeline.render(same_file.data() + (size_t)at * 2, total_halves - at < 100 ? total_halves - at : 100);
	CHECK(fake_audio_seek_count() <= 4);
	// Where the second clip is half in: the left channel is the first's sample (here 2880 + 960) times a gain plus the second's
	// (4800 + 960) times the other. The right channel is the tag 1, so it sums to the mix of two gains.
	const int s = 2880 + 960;
	CHECK(fabsf(same_file[(size_t)s * 2] - (0.70710678f * (float)s + 0.70710678f * (4800.0f + 960.0f))) < 0.5f);
	CHECK(fabsf(same_file[(size_t)s * 2 + 1] - 2.0f * 0.70710678f) < 1e-3f);
}

int main() {
	test_plays_clips_in_order();
	test_split_carries_on_without_fade_or_seek();
	test_reordered_clips_seek();
	test_render_in_small_blocks_matches();
	test_seek_and_the_end();
	test_cuts_make_no_click();
	test_files_without_sound();
	test_editing_keeps_readers();
	test_clips_at_other_speeds();
	test_fade_in_and_out_of_the_edit();
	test_crossfade_mix();
	if (g_failures == 0)
		printf("audio_timeline_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
