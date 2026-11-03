#include <stdio.h>
#include <stdint.h>

#include "timecode.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static std::string at_frame(int64_t frame, int num, int den, int64_t frame_ticks) {
	return timecode::format(timecode::for_ticks(frame * frame_ticks, num, den, frame_ticks));
}

// 29.97 fps: a tick is 1/30000 s and a frame lasts 1001 of them.
static void test_ntsc() {
	CHECK(at_frame(0, 1, 30000, 1001) == "0:00:00#0");
	CHECK(at_frame(1, 1, 30000, 1001) == "0:00:00#1");
	CHECK(at_frame(29, 1, 30000, 1001) == "0:00:00#29");   // 0.9677 s
	CHECK(at_frame(30, 1, 30000, 1001) == "0:00:01#0");    // 1.001 s
	CHECK(at_frame(59, 1, 30000, 1001) == "0:00:01#29");
	CHECK(at_frame(60, 1, 30000, 1001) == "0:00:02#0");

	// Walking through frames: the number counts up by one inside a second, starts again at 0 when
	// the second changes, and never reaches 30.
	Timecode previous = timecode::for_ticks(0, 1, 30000, 1001);
	int longest = 0;
	for (int64_t frame = 1; frame < 200000; frame++) {
		Timecode now = timecode::for_ticks(frame * 1001, 1, 30000, 1001);
		bool same_second = now.hours == previous.hours && now.minutes == previous.minutes && now.seconds == previous.seconds;
		if (same_second)
			CHECK(now.frame == previous.frame + 1);
		else
			CHECK(now.frame == 0);
		if (now.frame > longest)
			longest = now.frame;
		previous = now;
	}
	CHECK(longest == 29);

	// Over an hour in, the clock keeps real time (no drop-frame skipping).
	CHECK(at_frame(107892, 1, 30000, 1001) == "0:59:59#29");   // 3599.9964 s
	CHECK(at_frame(107893, 1, 30000, 1001) == "1:00:00#0");    // 3600.03 s
}

static void test_whole_rates() {
	CHECK(at_frame(0, 1, 90000, 3000) == "0:00:00#0");    // 30 fps
	CHECK(at_frame(29, 1, 90000, 3000) == "0:00:00#29");
	CHECK(at_frame(30, 1, 90000, 3000) == "0:00:01#0");
	CHECK(at_frame(59, 1, 60, 1) == "0:00:00#59");        // 60 fps, one tick a frame
	CHECK(at_frame(60 * 3661 + 7, 1, 60, 1) == "1:01:01#7");
	CHECK(at_frame(23, 1, 24, 1) == "0:00:00#23");
	CHECK(at_frame(24, 1, 24, 1) == "0:00:01#0");
}

static void test_odd_input() {
	CHECK(timecode::format(timecode::for_ticks(-5, 1, 30000, 1001)) == "0:00:00#0");
	CHECK(timecode::format(timecode::for_ticks(100, 0, 30000, 1001)) == "0:00:00#0");
	CHECK(timecode::format(timecode::for_ticks(100, 1, 30000, 0)) == "0:00:00#0");
	// A time a little off a frame boundary still belongs to the nearest frame.
	CHECK(timecode::format(timecode::for_ticks(30 * 1001 + 5, 1, 30000, 1001)) == "0:00:01#0");
}

static void test_padded_hours() {
	Timecode time = timecode::for_ticks(3 * 3600 * 90000 + 5 * 60 * 90000 + 7 * 90000 + 4 * 3000, 1, 90000, 3000);
	CHECK(timecode::format(time) == "3:05:07#4");
	CHECK(timecode::format(time, 2) == " 3:05:07#4");
	CHECK(timecode::format(time, 3) == "  3:05:07#4");
	Timecode late = timecode::for_ticks((int64_t)12 * 3600 * 90000, 1, 90000, 3000);
	CHECK(timecode::format(late, 2) == "12:00:00#0");
	CHECK(timecode::format(late, 1) == "12:00:00#0");
}

static void test_frame_rate_text() {
	CHECK(timecode::format_frame_rate(1, 30000, 1001) == "29.97");
	CHECK(timecode::format_frame_rate(1, 60000, 1001) == "59.94");
	CHECK(timecode::format_frame_rate(1, 24000, 1001) == "23.98");
	CHECK(timecode::format_frame_rate(1, 90000, 3000) == "30");
	CHECK(timecode::format_frame_rate(1, 60, 1) == "60");
	CHECK(timecode::format_frame_rate(1, 25, 1) == "25");
	CHECK(timecode::format_frame_rate(0, 25, 1) == "?");
}

int main() {
	test_ntsc();
	test_whole_rates();
	test_odd_input();
	test_padded_hours();
	test_frame_rate_text();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
