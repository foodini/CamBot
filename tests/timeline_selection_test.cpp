// Which clip the timeline's edits act on (timeline_selection.h).
#include <stdio.h>
#include <vector>

#include "edit_model.h"
#include "timeline_selection.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static EditList three_clips() {
	EditList list;
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));       // Timeline 0 .. 100.
	clips.push_back(Clip(0, 100, 300));     // 100 .. 300.
	clips.push_back(Clip(1, 0, 50));        // 300 .. 350.
	list.set(clips);
	return list;
}

static void test_following_the_playhead() {
	const EditList list = three_clips();
	TimelineSelection selection;
	CHECK(selection.kind() == TimelineSelection::NOTHING);
	CHECK(selection.effective_clip(list, 0) == 0);
	CHECK(selection.effective_clip(list, 99) == 0);
	CHECK(selection.effective_clip(list, 100) == 1);
	CHECK(selection.effective_clip(list, 349) == 2);
	// The end, and nothing at all.
	CHECK(selection.effective_clip(list, 350) == list.clip_index_for(350));
	EditList empty;
	CHECK(selection.effective_clip(empty, 0) == -1);
}

static void test_a_clicked_clip_is_kept_while_the_playhead_is_in_it() {
	const EditList list = three_clips();
	TimelineSelection selection;
	selection.select_clip(1);
	CHECK(selection.kind() == TimelineSelection::CLIP && selection.picked_clip() == 1 && selection.picked_seam() == -1);
	CHECK(selection.effective_clip(list, 150) == 1);
	// The playhead out of it: the clip it is in is the one again (and the pick is still there to come back to).
	CHECK(selection.effective_clip(list, 50) == 0);
	CHECK(selection.effective_clip(list, 320) == 2);
	CHECK(selection.effective_clip(list, 250) == 1);
}

static void test_in_a_crossfade() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));       // Timeline 0 .. 100.
	clips.push_back(Clip(0, 100, 300));     // 80 .. 280 (the last 20 of the first are under the first 20 of this).
	std::vector<Transition> transitions(3);
	transitions[1] = Transition(TRANSITION_CROSSFADE, 20);
	EditList list;
	list.set(clips, transitions);
	TimelineSelection selection;
	// Not picked: the one showing more of itself.
	CHECK(selection.effective_clip(list, 79) == 0);
	CHECK(selection.effective_clip(list, 89) == 0);
	CHECK(selection.effective_clip(list, 90) == 1);
	CHECK(selection.effective_clip(list, 99) == 1);
	// Picked: that one, wherever in it the playhead is.
	selection.select_clip(0);
	CHECK(selection.effective_clip(list, 95) == 0);
	CHECK(selection.effective_clip(list, 99) == 0);
	CHECK(selection.effective_clip(list, 100) == 1);      // Out of the first clip.
	selection.select_clip(1);
	CHECK(selection.effective_clip(list, 80) == 1);
	CHECK(selection.effective_clip(list, 85) == 1);
	CHECK(selection.effective_clip(list, 79) == 0);
}

static void test_a_transition_pick() {
	const EditList list = three_clips();
	TimelineSelection selection;
	selection.select_transition(2);
	CHECK(selection.kind() == TimelineSelection::TRANSITION && selection.picked_seam() == 2 && selection.picked_clip() == -1);
	// It does not stand in the way of the edits: the clip under the playhead is the one.
	CHECK(selection.effective_clip(list, 310) == 2);
	selection.select_clip(0);
	CHECK(selection.picked_seam() == -1 && selection.picked_clip() == 0);
	selection.clear();
	CHECK(selection.kind() == TimelineSelection::NOTHING);
}

static void test_changes_to_the_clips_let_go() {
	EditList list = three_clips();
	TimelineSelection selection;
	selection.note_clips(list);
	selection.select_clip(2);
	selection.note_clips(list);                // Nothing changed.
	CHECK(selection.picked_clip() == 2);
	std::vector<Clip> clips = list.clips();
	clips[0].out_ticks = 90;                   // A trim.
	list.set(clips);
	selection.note_clips(list);
	CHECK(selection.kind() == TimelineSelection::NOTHING);
	selection.select_transition(1);
	clips[1].speed = 2.0;                      // A speed change.
	list.set(clips);
	selection.note_clips(list);
	CHECK(selection.kind() == TimelineSelection::NOTHING);
}

int main() {
	test_following_the_playhead();
	test_a_clicked_clip_is_kept_while_the_playhead_is_in_it();
	test_in_a_crossfade();
	test_a_transition_pick();
	test_changes_to_the_clips_let_go();
	if (g_failures == 0)
		printf("timeline_selection_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
