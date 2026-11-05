// Where the playhead lands on the edit when the sync view is done with: the frame that was on screen.
#include <stdio.h>
#include <string>
#include <vector>

#include "edit_model.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

int main() {
	// Three files of 1000 ticks, in the order 0, 1, 2.
	EditModel model;
	std::vector<std::string> paths;
	paths.push_back("a.mp4");
	paths.push_back("b.mp4");
	paths.push_back("c.mp4");
	std::vector<int64_t> durations(3, 1000);
	model.set_media(paths, durations, 0.001);
	CHECK(model.timeline_ticks_nearest(FileTime(1, 250)) == 1250);   // Every file whole: the flight clock is the timeline.

	// An edit that uses 100..400 of file 0 and 500..900 of file 2, and cuts the rest.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 400));
	clips.push_back(Clip(2, 500, 900));
	CHECK(model.set_clips(clips));
	// Footage the edit uses is found where it is.
	CHECK(model.timeline_ticks_nearest(FileTime(0, 100)) == 0);
	CHECK(model.timeline_ticks_nearest(FileTime(0, 250)) == 150);
	CHECK(model.timeline_ticks_nearest(FileTime(2, 500)) == 300);
	CHECK(model.timeline_ticks_nearest(FileTime(2, 600)) == 400);
	// Footage that was cut goes to the closest footage that is left: before the first clip, between them, and after the last.
	CHECK(model.timeline_ticks_nearest(FileTime(0, 10)) == 0);
	CHECK(model.timeline_ticks_nearest(FileTime(0, 900)) == 300);    // Just past the first clip's end (closer to it than to the second).
	CHECK(model.timeline_ticks_nearest(FileTime(2, 100)) == 300);    // Just before the second clip.
	CHECK(model.timeline_ticks_nearest(FileTime(2, 990)) == 700);    // After the last clip: its end.
	CHECK(model.timeline_ticks_nearest(FileTime(1, 100)) == 300);    // File 1 is all cut: nearer the end of file 0's clip than the start of file 2's.
	CHECK(model.timeline_ticks_nearest(FileTime(1, 900)) == 300);    // ... and this is nearer the start of the clip in file 2 (the same place).

	// The order of the files changes where the same moment is on the flight clock: file 2 first.
	std::vector<int> order;
	order.push_back(2);
	order.push_back(0);
	order.push_back(1);
	CHECK(model.set_capture_order(order));
	CHECK(model.timeline_ticks_nearest(FileTime(2, 600)) == 400);    // Still in the second clip.
	CHECK(model.timeline_ticks_nearest(FileTime(0, 250)) == 150);    // Still in the first.
	CHECK(model.timeline_ticks_nearest(FileTime(1, 100)) == 300);    // File 1 is now last on the clock: the end of the clip that is later on it (file 0's).

	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
