// Tests MediaDecoder's logic against fake_ffmpeg.cpp's synthetic files: reading across file joins,
// draining each file's decoder, seeking, the frame cache, and audio timestamps.
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "fake_ffmpeg.h"
#include "media_decoder.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

static const int FT = 30;  // Ticks per frame in the fake files.

struct Setup {
	std::vector<std::string> paths;
	std::vector<int> frames;  // Per file.
	std::vector<int> gops;
	int total() const { int t = 0; for (size_t i = 0; i < frames.size(); i++) t += frames[i]; return t; }
};

static Setup make_setup(const int* frames, const int* gops, const int* delays, int count, bool audio = true) {
	fake_ffmpeg_reset();
	Setup s;
	for (int i = 0; i < count; i++) {
		FakeFileSpec spec;
		spec.frames = frames[i]; spec.gop = gops[i]; spec.delay = delays[i]; spec.tag = i; spec.audio = audio;
		std::string path = "file" + std::to_string(i) + ".mp4";
		fake_ffmpeg_add_file(path, spec);
		s.paths.push_back(path);
		s.frames.push_back(frames[i]);
		s.gops.push_back(gops[i]);
	}
	return s;
}

// Which global frame number is on screen, worked out from what's in the picture: the frame
// number within its file, and the file (the tag), plus where the files begin.
static int shown_frame(const MediaDecoder& decoder, const Setup& setup, int* file_out = nullptr) {
	const AVFrame* f = decoder.frame();
	int local = f->data[0][0] | (f->data[0][1] << 8);
	int file = f->data[1][0];
	int base = 0;
	for (int i = 0; i < file; i++) base += setup.frames[i];
	if (file_out) *file_out = file;
	return base + local;
}

static bool is_consistent(const MediaDecoder& decoder, const Setup& setup) {
	// The picture and the clock must agree.
	return decoder.pts() == (int64_t)shown_frame(decoder, setup) * FT;
}

static void test_single_file_sequential() {
	int frames[] = { 100 }, gops[] = { 10 }, delays[] = { 2 };
	Setup s = make_setup(frames, gops, delays, 1);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(decoder.total_ticks() == 100 * FT);
	CHECK(decoder.frame_ticks() == FT);
	CHECK(decoder.pts() == 0);
	int count = 1;
	while (decoder.read_frame()) {
		CHECK_MSG(shown_frame(decoder, s) == count, "expected %d got %d", count, shown_frame(decoder, s));
		CHECK(is_consistent(decoder, s));
		count++;
	}
	CHECK(count == 100);
	CHECK(decoder.pts() == 99 * FT);
	CHECK(!decoder.read_frame());   // Stays at the end.
	CHECK(shown_frame(decoder, s) == 99);
}

static void test_multi_file_sequential_no_frames_lost() {
	int frames[] = { 50, 40, 30 }, gops[] = { 10, 7, 10 }, delays[] = { 2, 0, 3 };
	Setup s = make_setup(frames, gops, delays, 3);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(decoder.file_count() == 3);
	CHECK(decoder.total_ticks() == 120 * FT);
	int count = 1;
	while (decoder.read_frame()) {
		CHECK_MSG(shown_frame(decoder, s) == count, "expected %d got %d", count, shown_frame(decoder, s));
		CHECK(is_consistent(decoder, s));
		count++;
	}
	CHECK_MSG(count == 120, "frames read: %d", count);
	CHECK(decoder.current_file() == 2);
}

static void test_nonzero_start_times() {
	fake_ffmpeg_reset();
	std::vector<std::string> paths;
	int tags[] = { 0, 1 };
	long long starts[] = { 0, 9000 };
	for (int i = 0; i < 2; i++) {
		FakeFileSpec spec;
		spec.frames = 30; spec.gop = 10; spec.tag = tags[i]; spec.start_time = starts[i]; spec.audio_start = starts[i] * 80;
		std::string path = "s" + std::to_string(i) + ".mp4";
		fake_ffmpeg_add_file(path, spec);
		paths.push_back(path);
	}
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(paths, &error));
	int count = 1;
	while (decoder.read_frame()) {
		CHECK_MSG(decoder.pts() == (int64_t)count * FT, "pts %lld expected %d", (long long)decoder.pts(), count * FT);
		count++;
	}
	CHECK(count == 60);
	// Seeking inside the second file lands on the right frame despite its start time.
	CHECK(decoder.advance_to(45 * FT));
	CHECK(decoder.pts() == 45 * FT);
	const AVFrame* f = decoder.frame();
	CHECK(f->data[1][0] == 1 && f->data[0][0] == 15);
}

static void test_advance_to_every_frame() {
	int frames[] = { 50, 40, 30 }, gops[] = { 10, 7, 10 }, delays[] = { 2, 0, 3 };
	Setup s = make_setup(frames, gops, delays, 3);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	// Forward, backward, and a scattering, all exact.
	std::vector<int> order;
	for (int i = 0; i < 120; i++) order.push_back(i);
	for (int i = 119; i >= 0; i--) order.push_back(i);
	srand(7);
	for (int i = 0; i < 300; i++) order.push_back(rand() % 120);
	for (size_t i = 0; i < order.size(); i++) {
		CHECK(decoder.advance_to((int64_t)order[i] * FT));
		CHECK_MSG(shown_frame(decoder, s) == order[i], "target %d got %d", order[i], shown_frame(decoder, s));
		CHECK(is_consistent(decoder, s));
	}
	// Past the end lands on the last frame.
	CHECK(decoder.advance_to(100000));
	CHECK(shown_frame(decoder, s) == 119);
	CHECK(decoder.advance_to(-500));
	CHECK(shown_frame(decoder, s) == 0);
}

static void test_stepping_across_joins_and_cache() {
	int frames[] = { 50, 40, 30 }, gops[] = { 10, 7, 10 }, delays[] = { 2, 0, 3 };
	Setup s = make_setup(frames, gops, delays, 3);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(decoder.advance_to(47 * FT));
	// Forward over the first join, one frame at a time.
	for (int n = 48; n <= 55; n++) {
		CHECK(decoder.step_frames(1));
		CHECK_MSG(shown_frame(decoder, s) == n, "forward: expected %d got %d", n, shown_frame(decoder, s));
	}
	// And back over it again.
	for (int n = 54; n >= 45; n--) {
		CHECK(decoder.step_frames(-1));
		CHECK_MSG(shown_frame(decoder, s) == n, "back: expected %d got %d", n, shown_frame(decoder, s));
	}
	// The sequence that exposed a bug in the original cache: step past what's cached, back into it,
	// and forward again past its end.
	CHECK(decoder.advance_to(20 * FT));
	for (int i = 0; i < 4; i++) {
		CHECK(decoder.step_frames(1));
		CHECK(shown_frame(decoder, s) == 21 + i);
	}
	for (int i = 0; i < 3; i++) {
		CHECK(decoder.step_frames(-1));
		CHECK(shown_frame(decoder, s) == 23 - i);
	}
	for (int i = 0; i < 6; i++) {
		CHECK(decoder.step_frames(1));
		CHECK_MSG(shown_frame(decoder, s) == 22 + i, "expected %d got %d", 22 + i, shown_frame(decoder, s));
	}
}

// Files whose stated length is longer or shorter than their frames really run (real cameras do
// this), so the join has a gap or an overlap. Stepping back across it must still reach the last
// frame of the earlier file, and stepping forward must still reach the first of the next.
static void test_stepping_across_joins_with_wrong_durations() {
	const long long extras[] = { 10, 20, 31, 45, 60, 75, 100, -10 };
	for (size_t e = 0; e < sizeof(extras) / sizeof(extras[0]); e++) {
		int frames[] = { 40, 30 }, gops[] = { 10, 10 }, delays[] = { 2, 2 };
		Setup s = make_setup(frames, gops, delays, 2);
		fake_ffmpeg_reset();
		for (int i = 0; i < 2; i++) {
			FakeFileSpec spec;
			spec.frames = frames[i]; spec.gop = gops[i]; spec.delay = delays[i]; spec.tag = i;
			spec.duration_extra = (i == 0) ? extras[e] : 0;
			fake_ffmpeg_add_file(s.paths[i], spec);
		}
		MediaDecoder decoder;
		std::string error;
		CHECK(decoder.open(s.paths, &error));
		// Land on the first frame of the second file by asking for its time, whatever the gap.
		CHECK(decoder.advance_to(decoder.timeline().file(1).start_ticks));
		int file = -1;
		int frame = shown_frame(decoder, s, &file);
		CHECK_MSG(file == 1, "extra %lld: expected file 1, got %d", extras[e], file);
		(void)frame;
		// One step back must land on the last frame of the first file (global frame 39).
		CHECK(decoder.step_frames(-1));
		int back_file = -1;
		shown_frame(decoder, s, &back_file);
		CHECK_MSG(back_file == 0, "extra %lld: step back stayed in file %d (frame %d)", extras[e], back_file, shown_frame(decoder, s));
		CHECK_MSG(shown_frame(decoder, s) == 39, "extra %lld: step back showed frame %d", extras[e], shown_frame(decoder, s));
		// And one more, then forward twice, over the join again.
		CHECK(decoder.step_frames(-1));
		CHECK_MSG(shown_frame(decoder, s) == 38, "extra %lld: second step back showed %d", extras[e], shown_frame(decoder, s));
		CHECK(decoder.step_frames(1));
		CHECK(decoder.step_frames(1));
		CHECK_MSG(shown_frame(decoder, s) == 40, "extra %lld: forward over the join showed %d", extras[e], shown_frame(decoder, s));
	}
}

// Garmin VIRB files flag their last video packet "discard" (the MP4 edit list ends one frame
// early), and FFmpeg then drops that frame, losing one picture at every join. The decoder has to
// show it anyway.
static void test_discard_flagged_last_frames_are_kept() {
	int frames[] = { 40, 25, 30 };
	fake_ffmpeg_reset();
	Setup s;
	for (int i = 0; i < 3; i++) {
		FakeFileSpec spec;
		spec.frames = frames[i]; spec.gop = 10; spec.delay = 2; spec.tag = i; spec.discard_last = true;
		std::string path = "file" + std::to_string(i) + ".mp4";
		fake_ffmpeg_add_file(path, spec);
		s.paths.push_back(path);
		s.frames.push_back(frames[i]);
		s.gops.push_back(10);
	}
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	// Playing straight through shows every frame of every file, in order.
	int expected = 0;
	CHECK(shown_frame(decoder, s) == expected);
	while (decoder.read_frame()) {
		expected++;
		CHECK_MSG(shown_frame(decoder, s) == expected, "play: expected %d got %d", expected, shown_frame(decoder, s));
		if (g_failures > 5) break;
	}
	CHECK_MSG(expected == s.total() - 1, "play ended at frame %d of %d", expected, s.total());
	// Stepping back over both joins reaches the last frame of each earlier file.
	CHECK(decoder.advance_to(65 * FT));
	for (int n = 64; n >= 38; n--) {
		CHECK(decoder.step_frames(-1));
		CHECK_MSG(shown_frame(decoder, s) == n, "back: expected %d got %d", n, shown_frame(decoder, s));
	}
}

static void test_read_frame_after_cache_hit_continues_from_screen() {
	// If the picture on screen came from the cache, playing on must continue from *it*, not
	// from wherever the decoder had got to.
	int frames[] = { 60 }, gops[] = { 20 }, delays[] = { 2 };
	Setup s = make_setup(frames, gops, delays, 1);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(decoder.advance_to(30 * FT));   // Decodes 20..30 and caches them.
	CHECK(decoder.advance_to(22 * FT));   // From the cache; the decoder is still at 30.
	CHECK(shown_frame(decoder, s) == 22);
	for (int n = 23; n <= 40; n++) {
		CHECK(decoder.read_frame());
		CHECK_MSG(shown_frame(decoder, s) == n, "expected %d got %d", n, shown_frame(decoder, s));
	}
}

static void test_seek_keyframe() {
	int frames[] = { 50, 40, 30 }, gops[] = { 10, 7, 10 }, delays[] = { 2, 0, 3 };
	Setup s = make_setup(frames, gops, delays, 3);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	// Global frame 57 is local frame 7 of file 1 (gop 7): keyframe at local 7 -> global 57.
	CHECK(decoder.seek_keyframe(57 * FT));
	CHECK(shown_frame(decoder, s) == 57);
	// Global 60 is local 10 of file 1: keyframe at local 7 -> 57.
	CHECK(decoder.seek_keyframe(60 * FT));
	CHECK(shown_frame(decoder, s) == 57);
	// Global 49 is the end of file 0: keyframe at 40.
	CHECK(decoder.seek_keyframe(49 * FT));
	CHECK(shown_frame(decoder, s) == 40);
	// Playing on from a keyframe seek works.
	CHECK(decoder.read_frame());
	CHECK(shown_frame(decoder, s) == 41);
}

static void test_fast_seek_by_frames() {
	int frames[] = { 50, 40, 30 }, gops[] = { 10, 7, 10 }, delays[] = { 2, 0, 3 };
	Setup s = make_setup(frames, gops, delays, 3);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(decoder.advance_to(12 * FT));
	CHECK(decoder.seek_by_frames_fast(3));          // Still inside the GOP: an exact step.
	CHECK(shown_frame(decoder, s) == 15);
	CHECK(decoder.seek_by_frames_fast(10));         // A key frame (20) is in the way: lands on it.
	CHECK(shown_frame(decoder, s) == 20);
	CHECK(decoder.seek_by_frames_fast(40));         // Across the join into the second file.
	int landed = shown_frame(decoder, s);
	CHECK_MSG(landed >= 50 && landed <= 60, "landed %d", landed);
	CHECK(decoder.seek_by_frames_fast(-5));
	CHECK(shown_frame(decoder, s) <= landed);
}

static void test_random_operations() {
	int frames[] = { 50, 40, 30, 17 }, gops[] = { 10, 7, 10, 5 }, delays[] = { 2, 0, 3, 1 };
	Setup s = make_setup(frames, gops, delays, 4);
	int total = s.total();
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	srand(getenv("SEED") ? atoi(getenv("SEED")) : 12345);
	int displayed = 0;
	for (int op = 0; op < 6000; op++) {
		int kind = rand() % 10;
		if (kind < 3) {
			int n = rand() % total;
			CHECK(decoder.advance_to((int64_t)n * FT));
			displayed = n;
			CHECK_MSG(shown_frame(decoder, s) == n, "op %d advance_to(%d) showed %d", op, n, shown_frame(decoder, s));
		} else if (kind < 6) {
			int k = (rand() % 9) - 4;
			if (k == 0) continue;
			int want = displayed + k;
			if (want < 0) want = 0;
			if (want > total - 1) want = total - 1;
			decoder.step_frames(k);
			displayed = want;
			CHECK_MSG(shown_frame(decoder, s) == want, "op %d step(%d) showed %d wanted %d", op, k, shown_frame(decoder, s), want);
		} else if (kind < 8) {
			bool more = decoder.read_frame();
			if (displayed + 1 < total) {
				CHECK_MSG(more, "op %d read_frame ended early at %d", op, displayed);
				displayed++;
			} else {
				CHECK(!more);
			}
			CHECK_MSG(shown_frame(decoder, s) == displayed, "op %d read_frame showed %d wanted %d", op, shown_frame(decoder, s), displayed);
		} else if (kind < 9) {
			int n = rand() % total;
			CHECK(decoder.seek_keyframe((int64_t)n * FT));
			// Expected: the latest key frame at or before n inside n's file.
			int base = 0, file = 0;
			while (n >= base + s.frames[file]) { base += s.frames[file]; file++; }
			int local = n - base;
			int expected = base + local - (local % s.gops[file]);
			displayed = expected;
			CHECK_MSG(shown_frame(decoder, s) == expected, "op %d seek_keyframe(%d) showed %d wanted %d", op, n, shown_frame(decoder, s), expected);
		} else {
			int k = (rand() % 41) - 20;
			if (k == 0) continue;
			decoder.seek_by_frames_fast(k);
			displayed = shown_frame(decoder, s);   // Approximate by design; just must be a valid frame.
			CHECK(displayed >= 0 && displayed < total);
		}
		CHECK_MSG(is_consistent(decoder, s), "op %d picture/clock mismatch", op);
		if (g_failures > 20) return;
	}
}

static void test_audio_timestamps() {
	// Three files, with sound; the second has an audio start offset of its own.
	fake_ffmpeg_reset();
	std::vector<std::string> paths;
	int frames[] = { 20, 30, 10 };
	long long audio_starts[] = { 0, 1234, 0 };
	for (int i = 0; i < 3; i++) {
		FakeFileSpec spec;
		spec.frames = frames[i]; spec.gop = 5; spec.tag = i; spec.audio_start = audio_starts[i];
		std::string path = "a" + std::to_string(i) + ".mp4";
		fake_ffmpeg_add_file(path, spec);
		paths.push_back(path);
	}
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(paths, &error));
	CHECK(decoder.has_audio());
	CHECK(decoder.audio_time_base().den == 48000);

	decoder.begin_audio_capture();
	CHECK(decoder.seek_keyframe(0));
	std::vector<long long> pts;
	while (true) {
		AVPacket* p;
		while ((p = decoder.pop_audio_packet()) != nullptr) {
			pts.push_back(p->pts);
			CHECK(p->dts == p->pts);
			av_packet_free(&p);
		}
		if (!decoder.read_frame())
			break;
	}
	AVPacket* p;
	while ((p = decoder.pop_audio_packet()) != nullptr) { pts.push_back(p->pts); av_packet_free(&p); }
	decoder.end_audio_capture();

	// Per file: frames/2 packets of 2400 samples, starting at the file's place on the global
	// clock (its start in ticks of 1/600 s, times 80 samples per tick).
	long long file_start_samples[] = { 0, 20 * FT * 80LL, (20 + 30) * FT * 80LL };
	std::vector<long long> expected;
	for (int f = 0; f < 3; f++)
		for (int k = 0; k < frames[f] / 2; k++)
			expected.push_back(file_start_samples[f] + (long long)k * 2400);
	CHECK_MSG(pts.size() == expected.size(), "audio packets %zu expected %zu", pts.size(), expected.size());
	for (size_t i = 0; i < pts.size() && i < expected.size(); i++)
		CHECK_MSG(pts[i] == expected[i], "audio packet %zu: %lld expected %lld", i, pts[i], expected[i]);

	// Not capturing: nothing is queued.
	CHECK(decoder.seek_keyframe(0));
	while (decoder.read_frame()) {}
	CHECK(decoder.pop_audio_packet() == nullptr);
}

static void test_no_audio_files() {
	int frames[] = { 10, 10 }, gops[] = { 5, 5 }, delays[] = { 1, 1 };
	Setup s = make_setup(frames, gops, delays, 2, false);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	CHECK(!decoder.has_audio());
	decoder.begin_audio_capture();
	while (decoder.read_frame()) {}
	CHECK(decoder.pop_audio_packet() == nullptr);
}

static void test_open_errors() {
	MediaDecoder decoder;
	std::string error;
	CHECK(!decoder.open(std::vector<std::string>(), &error));
	CHECK(!error.empty());

	fake_ffmpeg_reset();
	std::vector<std::string> paths;
	paths.push_back("missing.mp4");
	CHECK(!decoder.open(paths, &error));
	CHECK_MSG(error.find("missing.mp4") != std::string::npos, "%s", error.c_str());

	// A file that doesn't match the first.
	fake_ffmpeg_reset();
	FakeFileSpec a, b;
	b.width = 128; b.height = 96;
	fake_ffmpeg_add_file("a.mp4", a);
	fake_ffmpeg_add_file("b.mp4", b);
	paths.clear();
	paths.push_back("a.mp4");
	paths.push_back("b.mp4");
	CHECK(!decoder.open(paths, &error));
	CHECK_MSG(error.find("b.mp4") != std::string::npos && error.find("128x96") != std::string::npos, "%s", error.c_str());

	// Sound in one but not the other.
	fake_ffmpeg_reset();
	FakeFileSpec c, d;
	d.audio = false;
	fake_ffmpeg_add_file("c.mp4", c);
	fake_ffmpeg_add_file("d.mp4", d);
	paths.clear();
	paths.push_back("c.mp4");
	paths.push_back("d.mp4");
	CHECK(!decoder.open(paths, &error));
	CHECK_MSG(error.find("d.mp4") != std::string::npos, "%s", error.c_str());

	// And the same decoder object can be reused for a good open afterwards.
	fake_ffmpeg_reset();
	fake_ffmpeg_add_file("c.mp4", c);
	paths.clear();
	paths.push_back("c.mp4");
	CHECK(decoder.open(paths, &error));
	CHECK(decoder.is_open());
	CHECK(decoder.read_frame());
}

int main() {
	test_single_file_sequential();
	test_multi_file_sequential_no_frames_lost();
	test_nonzero_start_times();
	test_advance_to_every_frame();
	test_stepping_across_joins_and_cache();
	test_stepping_across_joins_with_wrong_durations();
	test_discard_flagged_last_frames_are_kept();
	test_read_frame_after_cache_hit_continues_from_screen();
	test_seek_keyframe();
	test_fast_seek_by_frames();
	test_random_operations();
	test_audio_timestamps();
	test_no_audio_files();
	test_open_errors();
	if (g_failures == 0)
		printf("decoder_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
