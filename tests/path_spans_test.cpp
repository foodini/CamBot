// Tests of path_spans: where a path shows on the edit timeline, given how the edit uses the footage it is on.

#include "path_spans.h"
#include <stdio.h>
#include <vector>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static EditList list_of(const std::vector<Clip>& clips) {
	EditList list;
	list.set(clips);
	return list;
}

static void test_one_clip() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 100, 1100));        // Timeline 0 .. 1000.
	const EditList list = list_of(clips);
	// Inside the clip.
	std::vector<text_lanes::Span> pieces = path_spans::pieces(list, 0, 300, 700);
	CHECK(pieces.size() == 1 && pieces[0].start == 200 && pieces[0].end == 600);
	// Hanging out of both ends: cut to the clip.
	pieces = path_spans::pieces(list, 0, 0, 5000);
	CHECK(pieces.size() == 1 && pieces[0].start == 0 && pieces[0].end == 1000);
	// Before the clip, after it, another file, backwards.
	CHECK(path_spans::pieces(list, 0, 0, 50).empty());
	CHECK(path_spans::pieces(list, 0, 1100, 1500).empty());
	CHECK(path_spans::pieces(list, 1, 300, 700).empty());
	CHECK(path_spans::pieces(list, 0, 700, 300).empty());
	// A single point is a stretch with no length.
	pieces = path_spans::pieces(list, 0, 400, 400);
	CHECK(pieces.size() == 1 && pieces[0].start == 300 && pieces[0].end == 300);
}

static void test_split_and_cut() {
	// The file's 0..400 and 600..1000 used, the middle cut out.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 400));           // Timeline 0 .. 400.
	clips.push_back(Clip(0, 600, 1000));        // 400 .. 800.
	const EditList list = list_of(clips);
	// A path from 300 to 700 has two pieces; the cut-out part of it is not on the timeline.
	std::vector<text_lanes::Span> pieces = path_spans::pieces(list, 0, 300, 700);
	CHECK(pieces.size() == 2);
	CHECK(pieces[0].start == 300 && pieces[0].end == 400);
	CHECK(pieces[1].start == 400 && pieces[1].end == 500);
	// One entirely in the cut-out part.
	CHECK(path_spans::pieces(list, 0, 450, 550).empty());
}

static void test_reordered_and_other_files() {
	std::vector<Clip> clips;
	clips.push_back(Clip(1, 0, 200));           // Timeline 0 .. 200, another file.
	clips.push_back(Clip(0, 500, 900));         // 200 .. 600.
	clips.push_back(Clip(0, 0, 300));           // 600 .. 900: earlier footage, later in the edit.
	const EditList list = list_of(clips);
	// A path that begins early in the file and runs on past 500 is only in the clip of 0..300 up to 300, and in the one of 500.. from 500.
	std::vector<text_lanes::Span> pieces = path_spans::pieces(list, 0, 100, 700);
	CHECK(pieces.size() == 2);
	CHECK(pieces[0].start == 200 && pieces[0].end == 400);      // 500..700 of the file.
	CHECK(pieces[1].start == 700 && pieces[1].end == 900);      // 100..300 of the file.
}

static void test_speed() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000, 2.0, AUDIO_KEEP_PITCH));    // Twice as fast: the timeline 0 .. 500.
	const EditList list = list_of(clips);
	const std::vector<text_lanes::Span> pieces = path_spans::pieces(list, 0, 200, 600);
	CHECK(pieces.size() == 1 && pieces[0].start == 100 && pieces[0].end == 300);
}

int main() {
	test_one_clip();
	test_split_and_cut();
	test_reordered_and_other_files();
	test_speed();
	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
