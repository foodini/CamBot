#include <stdio.h>
#include <vector>

#include "edit_history.h"
#include "timeline_edit.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

// One frame is 100 ticks. Three clips: A from file 0 (0..1000, 10 frames), B from file 1 (200..700, 5 frames),
// C from file 0 again (2000..2300, 3 frames). On the timeline they start at 0, 1000 and 1500 and it ends at 1800.
static const int64_t FT = 100;

static EditList three_clips() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 200, 700));
	clips.push_back(Clip(0, 2000, 2300));
	EditList list;
	list.set(clips);
	return list;
}

static int64_t total_of(const std::vector<Clip>& clips) {
	int64_t total = 0;
	for (size_t i = 0; i < clips.size(); i++)
		total += clips[i].duration_ticks();
	return total;
}

static void test_current_clip() {
	EditList list = three_clips();
	CHECK(timeline_edit::current_clip(list, 0) == 0);
	CHECK(timeline_edit::current_clip(list, 900) == 0);
	CHECK(timeline_edit::current_clip(list, 1000) == 1);
	CHECK(timeline_edit::current_clip(list, 1700) == 2);
	EditList empty;
	CHECK(timeline_edit::current_clip(empty, 0) == -1);
}

static void test_split() {
	EditList list = three_clips();
	// In the middle of A: the frame at 400 starts the second piece.
	timeline_edit::Result result = timeline_edit::split(list, 400, FT);
	CHECK(result.ok);
	CHECK(result.clips.size() == 4);
	CHECK(result.clips[0] == Clip(0, 0, 400));
	CHECK(result.clips[1] == Clip(0, 400, 1000));
	CHECK(result.clips[2] == Clip(1, 200, 700));
	CHECK(result.playhead_ticks == 400);
	CHECK(total_of(result.clips) == list.total_ticks());
	// In B (starts at 1000 on the timeline, at 200 in its file): the frame at 1200 is file time 400.
	result = timeline_edit::split(list, 1200, FT);
	CHECK(result.ok && result.clips.size() == 4);
	CHECK(result.clips[1] == Clip(1, 200, 400));
	CHECK(result.clips[2] == Clip(1, 400, 700));
	// On the second frame of a clip: fine. On the first frame, not.
	CHECK(timeline_edit::can_split(list, 100, FT));
	CHECK(!timeline_edit::can_split(list, 0, FT));
	CHECK(!timeline_edit::can_split(list, 1000, FT));
	CHECK(!timeline_edit::split(list, 1000, FT).ok);
	// A frame a few ticks off its nominal time still counts as the first frame.
	CHECK(!timeline_edit::can_split(list, 1003, FT));
	CHECK(timeline_edit::can_split(list, 1097, FT));    // The second frame, early by a few ticks.
	// The last frame can be split off.
	CHECK(timeline_edit::can_split(list, 1700, FT));
	result = timeline_edit::split(list, 1700, FT);
	CHECK(result.ok && result.clips[3] == Clip(0, 2200, 2300));
}

static void test_trim_left() {
	EditList list = three_clips();
	timeline_edit::Result result = timeline_edit::trim_left(list, 1300, FT);   // Frame at file time 500 of B.
	CHECK(result.ok);
	CHECK(result.clips.size() == 3);
	CHECK(result.clips[0] == Clip(0, 0, 1000));
	CHECK(result.clips[1] == Clip(1, 500, 700));
	CHECK(result.clips[2] == Clip(0, 2000, 2300));
	// The playhead's frame is now B's first, which begins where B began.
	CHECK(result.playhead_ticks == 1000);
	CHECK(total_of(result.clips) == list.total_ticks() - 300);
	// Nothing before the first frame to throw away.
	CHECK(!timeline_edit::can_trim_left(list, 1000, FT));
	CHECK(!timeline_edit::trim_left(list, 1000, FT).ok);
	CHECK(!timeline_edit::can_trim_left(list, 0, FT));
	CHECK(timeline_edit::can_trim_left(list, 1799, FT));
}

static void test_trim_right() {
	EditList list = three_clips();
	timeline_edit::Result result = timeline_edit::trim_right(list, 1100, FT);  // Frame at file time 300 of B.
	CHECK(result.ok);
	CHECK(result.clips[1] == Clip(1, 200, 400));    // Keeps the frame at 300 (it lasts until 400).
	CHECK(result.playhead_ticks == 1100);
	CHECK(total_of(result.clips) == list.total_ticks() - 300);
	// The last frame of a clip has nothing after it.
	CHECK(!timeline_edit::can_trim_right(list, 900, FT));
	CHECK(!timeline_edit::trim_right(list, 900, FT).ok);
	CHECK(!timeline_edit::can_trim_right(list, 1400, FT));
	CHECK(timeline_edit::can_trim_right(list, 800, FT));
	// The first frame can be kept alone.
	result = timeline_edit::trim_right(list, 0, FT);
	CHECK(result.ok && result.clips[0] == Clip(0, 0, 100));
	// The frame after a trim right is the next clip's first.
	timeline_edit::Result after = timeline_edit::trim_right(list, 1000, FT);
	CHECK(after.ok && after.clips[1] == Clip(1, 200, 300));
}

static void test_remove() {
	EditList list = three_clips();
	// Middle clip: the playhead goes to where it began, now the start of C.
	timeline_edit::Result result = timeline_edit::remove_clip(list, 1300, FT);
	CHECK(result.ok && result.clips.size() == 2);
	CHECK(result.clips[0] == Clip(0, 0, 1000));
	CHECK(result.clips[1] == Clip(0, 2000, 2300));
	CHECK(result.playhead_ticks == 1000);
	// First clip.
	result = timeline_edit::remove_clip(list, 500, FT);
	CHECK(result.ok && result.clips[0] == Clip(1, 200, 700) && result.playhead_ticks == 0);
	// Last clip: the playhead lands on the last frame left.
	result = timeline_edit::remove_clip(list, 1700, FT);
	CHECK(result.ok && result.clips.size() == 2);
	CHECK(result.playhead_ticks == 1400);
	// Never the only clip.
	std::vector<Clip> one;
	one.push_back(Clip(0, 0, 500));
	EditList single;
	single.set(one);
	CHECK(!timeline_edit::can_remove(single, 0, FT));
	CHECK(!timeline_edit::remove_clip(single, 0, FT).ok);
}

static void test_results_make_valid_lists() {
	// Whatever is done, the new clips are accepted by an EditList and the playhead is on a frame inside it.
	EditList list = three_clips();
	for (int64_t p = 0; p < list.total_ticks(); p += FT) {
		timeline_edit::Result results[4];
		results[0] = timeline_edit::split(list, p, FT);
		results[1] = timeline_edit::trim_left(list, p, FT);
		results[2] = timeline_edit::trim_right(list, p, FT);
		results[3] = timeline_edit::remove_clip(list, p, FT);
		for (int r = 0; r < 4; r++) {
			if (!results[r].ok)
				continue;
			EditList after;
			after.set(results[r].clips);
			for (int i = 0; i < after.count(); i++)
				CHECK(after.clip(i).out_ticks > after.clip(i).in_ticks);
			CHECK(results[r].playhead_ticks >= 0 && results[r].playhead_ticks < after.total_ticks());
		}
		// can_* agrees with the operations.
		CHECK(timeline_edit::can_split(list, p, FT) == results[0].ok);
		CHECK(timeline_edit::can_trim_left(list, p, FT) == results[1].ok);
		CHECK(timeline_edit::can_trim_right(list, p, FT) == results[2].ok);
		CHECK(timeline_edit::can_remove(list, p, FT) == results[3].ok);
	}
}

static EditState state(int64_t playhead, bool started, int64_t clip_end) {
	EditState s;
	s.clips.push_back(Clip(0, 0, clip_end));
	s.edit_started = started;
	s.playhead_ticks = playhead;
	return s;
}

static void test_history() {
	EditHistory history;
	CHECK(!history.can_undo() && !history.can_redo());
	EditState restored;
	CHECK(!history.undo(state(0, false, 100), &restored));

	history.record(state(10, false, 1000));    // Edit 1 starts from this.
	history.record(state(20, true, 900));      // Edit 2 starts from this.
	CHECK(history.can_undo() && !history.can_redo());

	// Undo edit 2: back to its "before", and the state we were in is kept for redo.
	CHECK(history.undo(state(30, true, 800), &restored));
	CHECK(restored.playhead_ticks == 20 && restored.edit_started && restored.clips[0].out_ticks == 900);
	CHECK(history.can_redo());
	// Undo edit 1.
	CHECK(history.undo(state(20, true, 900), &restored));
	CHECK(restored.playhead_ticks == 10 && !restored.edit_started && restored.clips[0].out_ticks == 1000);
	CHECK(!history.can_undo());
	// Redo both, in order.
	CHECK(history.redo(state(10, false, 1000), &restored));
	CHECK(restored.clips[0].out_ticks == 900 && restored.playhead_ticks == 20);
	CHECK(history.redo(state(20, true, 900), &restored));
	CHECK(restored.clips[0].out_ticks == 800 && restored.playhead_ticks == 30);
	CHECK(!history.can_redo());

	// A new edit after an undo ends the redo.
	CHECK(history.undo(state(30, true, 800), &restored));
	CHECK(history.can_redo());
	history.record(state(40, true, 700));
	CHECK(!history.can_redo());

	history.clear();
	CHECK(!history.can_undo() && !history.can_redo());
}

static void test_history_limit() {
	EditHistory history;
	for (int i = 0; i < EditHistory::MAX_STEPS + 50; i++)
		history.record(state(i, true, 1000));
	EditState restored;
	int steps = 0;
	int64_t oldest = -1;
	while (history.undo(state(0, true, 1000), &restored)) {
		steps++;
		oldest = restored.playhead_ticks;
	}
	CHECK(steps == EditHistory::MAX_STEPS);
	CHECK(oldest == 50);    // The first 50 were forgotten.
}

int main() {
	test_current_clip();
	test_split();
	test_trim_left();
	test_trim_right();
	test_remove();
	test_results_make_valid_lists();
	test_history();
	test_history_limit();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
