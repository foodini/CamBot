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

static void test_history_text_boxes() {
	// The text boxes are part of what an undo step keeps.
	EditState before = state(10, false, 1000);
	EditState after = before;
	TextBox box = text_box::default_box();
	box.id = 1;
	box.text = "typed";
	after.text_boxes.push_back(box);

	EditHistory history;
	history.record(before);
	EditState restored;
	CHECK(history.undo(after, &restored));
	CHECK(restored.text_boxes.empty());
	CHECK(history.redo(before, &restored));
	CHECK(restored.text_boxes.size() == 1 && restored.text_boxes[0] == box);
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

static void test_move_clip() {
	EditList list = three_clips();   // A 0..1000 at 0, B 200..700 at 1000, C 2000..2300 at 1500; ends at 1800.
	CHECK(!timeline_edit::can_move(list, 0, 0));
	CHECK(!timeline_edit::can_move(list, 3, 0));
	CHECK(!timeline_edit::can_move(list, 0, 3));
	CHECK(!timeline_edit::can_move(list, -1, 1));
	CHECK(!timeline_edit::move_clip(list, 1, 1, 0).ok);

	// A to the end: B C A. The playhead in B (at 1200, 200 into B) is now 200 into the new first clip.
	timeline_edit::Result result = timeline_edit::move_clip(list, 0, 2, 1200);
	CHECK(result.ok);
	CHECK(result.clips.size() == 3);
	CHECK(result.clips[0] == Clip(1, 200, 700));
	CHECK(result.clips[1] == Clip(0, 2000, 2300));
	CHECK(result.clips[2] == Clip(0, 0, 1000));
	CHECK(result.playhead_ticks == 200);
	CHECK(total_of(result.clips) == 1800);

	// The playhead in the moved clip goes with it: 300 into A (at 300) when A is last starts at 800 -> 1100.
	result = timeline_edit::move_clip(list, 0, 2, 300);
	CHECK(result.clips[2] == Clip(0, 0, 1000));
	CHECK(result.playhead_ticks == 800 + 300);

	// C to the front: C A B.
	result = timeline_edit::move_clip(list, 2, 0, 1700);
	CHECK(result.clips[0] == Clip(0, 2000, 2300));
	CHECK(result.clips[1] == Clip(0, 0, 1000));
	CHECK(result.clips[2] == Clip(1, 200, 700));
	CHECK(result.playhead_ticks == 200);   // 200 into C.

	// B to the front, playhead in A (500): B A C, A now starts at 500.
	result = timeline_edit::move_clip(list, 1, 0, 500);
	CHECK(result.clips[0] == Clip(1, 200, 700));
	CHECK(result.clips[1] == Clip(0, 0, 1000));
	CHECK(result.clips[2] == Clip(0, 2000, 2300));
	CHECK(result.playhead_ticks == 500 + 500);

	// Every move gives a valid list holding the same footage, and the playhead's frame comes from the same moment of the same file.
	for (int from = 0; from < 3; from++) {
		for (int to = 0; to < 3; to++) {
			if (from == to)
				continue;
			for (int64_t playhead = 0; playhead < 1800; playhead += 100) {
				timeline_edit::Result moved = timeline_edit::move_clip(list, from, to, playhead);
				CHECK(moved.ok && moved.clips.size() == 3 && total_of(moved.clips) == 1800);
				EditList after;
				after.set(moved.clips);
				FileTime before_moment = list.source_for(playhead);
				FileTime after_moment = after.source_for(moved.playhead_ticks);
				CHECK(before_moment == after_moment);
			}
		}
	}
}

static void test_drop_index() {
	EditList list = three_clips();   // Centers: A 500, B 1250, C 1650.
	// Dragging A: the others are B and C.
	CHECK(timeline_edit::drop_index(list, 0, 0.0) == 0);
	CHECK(timeline_edit::drop_index(list, 0, 1249.0) == 0);
	CHECK(timeline_edit::drop_index(list, 0, 1251.0) == 1);
	CHECK(timeline_edit::drop_index(list, 0, 1651.0) == 2);
	CHECK(timeline_edit::drop_index(list, 0, 99999.0) == 2);
	// Dragging C: the others are A and B.
	CHECK(timeline_edit::drop_index(list, 2, 499.0) == 0);
	CHECK(timeline_edit::drop_index(list, 2, 501.0) == 1);
	CHECK(timeline_edit::drop_index(list, 2, 1251.0) == 2);
	// Dropping where it already is gives its own index.
	CHECK(timeline_edit::drop_index(list, 1, 1250.0) == 1);
	// One clip: nowhere else to go.
	std::vector<Clip> one;
	one.push_back(Clip(0, 0, 100));
	EditList single;
	single.set(one);
	CHECK(timeline_edit::drop_index(single, 0, 50.0) == 0);
	CHECK(!timeline_edit::can_move(single, 0, 0));
}

static void test_drop_marker() {
	EditList list = three_clips();   // A 0..1000, B 1000..1500, C 1500..1800.
	// Dragging A: the others are B (1000..1500) and C (1500..1800).
	CHECK(timeline_edit::drop_marker_ticks(list, 0, 0) == 0);
	CHECK(timeline_edit::drop_marker_ticks(list, 0, 1) == 1500);
	CHECK(timeline_edit::drop_marker_ticks(list, 0, 2) == 1800);
	// Dragging C: the others are A and B.
	CHECK(timeline_edit::drop_marker_ticks(list, 2, 0) == 0);
	CHECK(timeline_edit::drop_marker_ticks(list, 2, 1) == 1000);
	CHECK(timeline_edit::drop_marker_ticks(list, 2, 2) == 1500);
	// Dragging B.
	CHECK(timeline_edit::drop_marker_ticks(list, 1, 1) == 1000);   // After A.
	CHECK(timeline_edit::drop_marker_ticks(list, 1, 2) == 1800);   // After C.
}

// Transitions go with the clips through the edits (A 0..1000, B 1000..1500 and C 1500..1800 on the timeline).
static EditList three_clips_with_fades() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 200, 700));
	clips.push_back(Clip(0, 2000, 2300));
	std::vector<Transition> transitions(4);
	transitions[0] = Transition(TRANSITION_FADE, 400);     // The most A can give: 500.
	transitions[3] = Transition(TRANSITION_FADE, 150);     // The most C can give: 150.
	EditList list;
	list.set(clips, transitions);
	return list;
}

static bool valid_after(const timeline_edit::Result& result) {
	EditList after;
	after.set(result.clips, result.transitions);
	return after.transitions_valid();
}

static void test_transitions_with_edits() {
	EditList list = three_clips_with_fades();
	CHECK(list.transitions_valid());
	// Split: the pieces keep what they can, the new join is a cut. A piece too short for the fade it would carry is not allowed.
	CHECK(!timeline_edit::can_split(list, 400, FT));       // The first piece, 400 long, has room for a fade of 200 only.
	CHECK(!timeline_edit::split(list, 400, FT).ok);
	timeline_edit::Result result = timeline_edit::split(list, 800, FT);
	CHECK(result.ok && timeline_edit::can_split(list, 800, FT));
	CHECK(result.clips.size() == 4 && result.transitions.size() == 5);
	CHECK(result.transitions[0] == list.transition(0) && result.transitions[1].is_cut() && result.transitions[2].is_cut());
	CHECK(result.transitions[4] == list.transition(3));
	CHECK(valid_after(result));
	// A split in the last clip hands the end on to the second piece.
	result = timeline_edit::split(list, 1700, FT);
	CHECK(!result.ok);                                      // 200 and 100 long: the end's 150 doesn't fit the second.
	// Trims.
	CHECK(!timeline_edit::can_trim_left(list, 500, FT));    // A would be 500 long, room for a fade of 250.
	CHECK(timeline_edit::can_trim_left(list, 100, FT));
	result = timeline_edit::trim_left(list, 100, FT);
	CHECK(result.ok && result.transitions == list.transitions() && valid_after(result));
	CHECK(!timeline_edit::can_trim_right(list, 1600, FT));  // C would be 200 long, room for 100.
	CHECK(!timeline_edit::trim_right(list, 1600, FT).ok);
	// A trim in the middle of B, which has no transitions, is free.
	CHECK(timeline_edit::can_trim_right(list, 1200, FT));
	CHECK(timeline_edit::can_trim_left(list, 1200, FT));
	// Speed.
	CHECK(!timeline_edit::can_set_speed(list, 100, 2.0, AUDIO_KEEP_PITCH));    // A would be 500 long.
	CHECK(timeline_edit::can_set_speed(list, 100, 0.5, AUDIO_KEEP_PITCH));
	CHECK(timeline_edit::can_set_speed(list, 1200, 2.0, AUDIO_KEEP_PITCH));    // B has no transition of its own.
	result = timeline_edit::set_speed(list, 100, 0.5, AUDIO_KEEP_PITCH, FT);
	CHECK(result.ok && result.transitions == list.transitions() && valid_after(result));
}

static void test_transitions_when_clips_come_and_go() {
	EditList list = three_clips_with_fades();
	// Removing a clip: the start and the end stay, the join it leaves is a cut.
	timeline_edit::Result result = timeline_edit::remove_clip(list, 1200, FT);      // B.
	CHECK(result.ok && result.transitions.size() == 3);
	CHECK(result.transitions[0] == list.transition(0) && result.transitions[1].is_cut() && result.transitions[2] == list.transition(3));
	CHECK(valid_after(result));
	// Removing the first clip hands the start to B, which has room for half its 500 only: the fade is shortened to fit.
	result = timeline_edit::remove_clip(list, 100, FT);
	CHECK(result.ok && result.transitions.size() == 3);
	CHECK(result.transitions[0].type == TRANSITION_FADE && result.transitions[0].ticks == 250);
	CHECK(result.transitions[2] == list.transition(3));
	CHECK(valid_after(result));
	// And the last: B now ends the video, with room for 250, so the 150 stays.
	result = timeline_edit::remove_clip(list, 1700, FT);
	CHECK(result.ok && result.transitions[2] == list.transition(3) && result.transitions[0] == list.transition(0));
	// Moving: the ends stay where they are, and a join is kept only if the same two clips are still together in the same order.
	// (Joins can't have a transition yet; a crossfade is put in here by hand to see where it goes.)
	std::vector<Clip> four;
	four.push_back(Clip(0, 0, 1000));
	four.push_back(Clip(1, 0, 1000));
	four.push_back(Clip(0, 1000, 2000));
	four.push_back(Clip(1, 1000, 2000));
	std::vector<Transition> marked(5);
	marked[3] = Transition(TRANSITION_CROSSFADE, 100);     // Between the third clip and the fourth.
	marked[0] = Transition(TRANSITION_FADE, 300);
	marked[4] = Transition(TRANSITION_FADE, 200);
	EditList long_list;
	long_list.set(four, marked);
	result = timeline_edit::move_clip(long_list, 0, 3, 0);          // The first clip to the end: B C D A.
	CHECK(result.ok && result.transitions.size() == 5);
	CHECK(result.transitions[0] == marked[0] && result.transitions[4] == marked[4]);
	CHECK(result.transitions[1].is_cut());                          // B|C kept the (cut) they had.
	CHECK(result.transitions[3].is_cut());                          // D|A is new.
	CHECK(result.transitions[2] == marked[3]);                      // C|D kept it.
	result = timeline_edit::move_clip(long_list, 1, 2, 0);          // The second clip after the third: A C B D.
	CHECK(result.ok);
	CHECK(!result.transitions[1].type && !result.transitions[2].type && !result.transitions[3].type);   // All cuts.
	CHECK(result.transitions[0] == marked[0] && result.transitions[4] == marked[4]);
	// Removing from the front of a list with a crossfade carries it to where it is now.
	result = timeline_edit::remove_clip(long_list, 100, FT);        // A: B C D.
	CHECK(result.ok && result.transitions.size() == 4);
	CHECK(result.transitions[2] == marked[3]);
}


// Edits of a named clip, and edits where clips overlap in a crossfade.
//   A: file 0, 0..1000 (timeline 0..1000). B: file 1, 0..1000, starting 200 before A ends (800..1800). C: file 0, 2000..3000, starting
//   300 before B ends (1500..2500).
static EditList crossfaded() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));
	clips.push_back(Clip(1, 0, 1000));
	clips.push_back(Clip(0, 2000, 3000));
	std::vector<Transition> transitions(4);
	transitions[1] = Transition(TRANSITION_CROSSFADE, 200);
	transitions[2] = Transition(TRANSITION_CROSSFADE, 300);
	EditList list;
	list.set(clips, transitions);
	return list;
}

static void test_named_clip() {
	const EditList list = three_clips();
	// The named clip is the one, if the playhead is in it; the playhead's own clip if none is named.
	timeline_edit::Result result = timeline_edit::trim_left(list, 1200, FT, 1);
	CHECK(result.ok && result.clips[1].in_ticks == 200 + 200);
	result = timeline_edit::trim_left(list, 1200, FT, 0);          // The playhead is not in A.
	CHECK(!result.ok);
	result = timeline_edit::trim_left(list, 1200, FT, 5);          // No such clip.
	CHECK(!result.ok);
	CHECK(!timeline_edit::can_split(list, 1200, FT, 2) && timeline_edit::can_split(list, 1200, FT, 1) && timeline_edit::can_split(list, 1200, FT, -1));
	CHECK(!timeline_edit::can_remove(list, 1200, FT, 0) && timeline_edit::can_remove(list, 1200, FT, 1));
	CHECK(!timeline_edit::can_set_speed(list, 1200, 2.0, AUDIO_KEEP_PITCH, 0) && timeline_edit::can_set_speed(list, 1200, 2.0, AUDIO_KEEP_PITCH, 1));
}

static void test_same_frame_after() {
	const EditList crossed = crossfaded();
	EditList plain;
	plain.set(crossed.clips());
	// Making the crossfades pulls what follows them earlier; the playhead goes with its frame (500 into C: 2500 -> 2000). Taking them away undoes it.
	CHECK(timeline_edit::same_frame_after(plain, crossed, 2500) == 2000);
	CHECK(timeline_edit::same_frame_after(crossed, plain, 2000) == 2500);
	CHECK(timeline_edit::same_frame_after(plain, crossed, 100) == 100);
	CHECK(timeline_edit::same_frame_after(plain, plain, 1234) == 1234);
}

static void test_edits_in_a_crossfade() {
	const EditList list = crossfaded();
	CHECK(list.start(1) == 800 && list.start(2) == 1500 && list.total_ticks() == 2500);
	// At 850 the playhead is in A and in B; the one that shows more of itself is A until 900, then B.
	CHECK(timeline_edit::current_clip(list, 850) == 0 && timeline_edit::current_clip(list, 950) == 1);
	// A trim right of A at 800 leaves A 900 long (up to the next frame), and what follows moves up with its crossfade.
	timeline_edit::Result result = timeline_edit::trim_right(list, 800, FT);
	CHECK(result.ok && result.clips[0].out_ticks == 900);
	EditList after;
	after.set(result.clips, result.transitions);
	CHECK(after.start(1) == 700 && after.total_ticks() == 2400 && result.playhead_ticks == 800);
	// A trim left of A at 850 is not possible: A would be 150 long, with a crossfade of 200 at its end.
	CHECK(!timeline_edit::trim_left(list, 850, FT).ok);
	// Naming B at the same place, it is 50 ticks into B, which has no whole frame before it.
	CHECK(!timeline_edit::trim_left(list, 830, FT, 1).ok);
}

static void test_edits_in_a_crossfade_more() {
	const EditList list = crossfaded();
	// Trim Left of B at 1000 (200 into it) is possible, and B keeps its start.
	timeline_edit::Result result = timeline_edit::trim_left(list, 1000, FT, 1);
	CHECK(result.ok && result.clips[1].in_ticks == 200 && result.playhead_ticks == 800);
	CHECK(result.transitions[1] == list.transition(1) && result.transitions[2] == list.transition(2));
	// Splitting B: each piece has to hold its crossfades (200 in; 300 out).
	CHECK(!timeline_edit::split(list, 1000, FT, 1).ok);            // First piece 200 long, crossfade 200: needs 400.
	CHECK(!timeline_edit::split(list, 1100, FT, 1).ok);            // 300 long: needs 400.
	result = timeline_edit::split(list, 1200, FT, 1);              // 400 long: just enough; the second piece is 600 and has the 300.
	CHECK(result.ok && result.clips.size() == 4 && result.clips[1].out_ticks == 400 && result.clips[2].in_ticks == 400);
	CHECK(result.transitions[1] == list.transition(1) && result.transitions[2].is_cut() && result.transitions[3] == list.transition(2));
	EditList split_list;
	split_list.set(result.clips, result.transitions);
	CHECK(result.playhead_ticks == split_list.start(2) && split_list.start(2) == 1200);
	CHECK(split_list.transitions_valid());
	// A change of speed that makes a clip too short for its crossfades is refused; one that leaves room is allowed.
	CHECK(!timeline_edit::can_set_speed(list, 1000, 8.0, AUDIO_KEEP_PITCH, 1));        // B would be 125 long.
	CHECK(!timeline_edit::can_set_speed(list, 1000, 2.0, AUDIO_KEEP_PITCH, 1));        // 500 long: half is 250, short of the 300.
	result = timeline_edit::set_speed(list, 1000, 1.5, AUDIO_KEEP_PITCH, FT, 1);       // 667 long: room for 300 and 200.
	CHECK(result.ok);
	EditList fast;
	fast.set(result.clips, result.transitions);
	CHECK(fast.transitions_valid() && fast.start(1) == 800 && fast.start(2) == 1167);
	// The playhead stays on the frame of footage: 200 in, which is now 133 timeline ticks into B.
	CHECK(result.playhead_ticks == 933);
	// Removing the middle clip: the crossfades go with it and the neighbours join with a cut.
	result = timeline_edit::remove_clip(list, 1200, FT, 1);
	CHECK(result.ok && result.clips.size() == 2 && result.transitions[1].is_cut());
	EditList closed;
	closed.set(result.clips, result.transitions);
	CHECK(closed.start(1) == 1000 && result.playhead_ticks == 1000 && closed.total_ticks() == 2000);
	// Removing the first clip: the second starts the timeline.
	result = timeline_edit::remove_clip(list, 100, FT);
	CHECK(result.ok && result.clips.size() == 2 && result.playhead_ticks == 0 && result.transitions[1] == list.transition(2));
	// Moving keeps a crossfade only between clips that are still together: A B C -> B A C breaks both.
	result = timeline_edit::move_clip(list, 0, 1, 100);
	CHECK(result.ok && result.transitions[1].is_cut() && result.transitions[2].is_cut());
	// Moving the last clip out and back round: C B A has no crossfades; B C A keeps B|C.
	result = timeline_edit::move_clip(list, 0, 2, 900);
	CHECK(result.ok && result.transitions[1] == list.transition(2) && result.transitions[2].is_cut());
	// The playhead at 900 is on B (it shows more of itself from there), 100 into it: B is first now, so that is 100.
	EditList moved;
	moved.set(result.clips, result.transitions);
	CHECK(result.playhead_ticks == 100 && result.playhead_ticks == moved.timeline_for(0, 100));
}

int main() {
	test_current_clip();
	test_split();
	test_trim_left();
	test_trim_right();
	test_remove();
	test_results_make_valid_lists();
	test_history();
	test_history_text_boxes();
	test_history_limit();
	test_move_clip();
	test_drop_index();
	test_drop_marker();
	test_transitions_with_edits();
	test_transitions_when_clips_come_and_go();
	test_named_clip();
	test_same_frame_after();
	test_edits_in_a_crossfade();
	test_edits_in_a_crossfade_more();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
