// The transitions of an edit (edit_model.h): which are allowed, how long they may be, how much of the picture shows during a fade,
// and how they go with the clips through the model and the source view.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "edit_model.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define CHECK_NEAR(a, b) do { double va = (a), vb = (b); if (fabs(va - vb) > 1e-6) { printf("FAIL line %d: %s = %.6f, wanted %.6f\n", __LINE__, #a, va, vb); fails++; } } while (0)

// Ticks are milliseconds in these tests, so "seconds * 1000".
static const double SPT = 0.001;
static int64_t S(double seconds) { return (int64_t)llround(seconds * 1000.0); }

static std::vector<int64_t> durations(double a, double b, double c, double d) {
	std::vector<int64_t> result;
	result.push_back(S(a)); result.push_back(S(b)); result.push_back(S(c)); result.push_back(S(d));
	return result;
}

static std::vector<std::string> names4() {
	std::vector<std::string> result;
	result.push_back("A.mp4"); result.push_back("B.mp4"); result.push_back("C.mp4"); result.push_back("D.mp4");
	return result;
}

static void test_transitions() {
	EditList list;
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));      // Timeline 0 .. 1000.
	clips.push_back(Clip(0, 1000, 1400));   // 1000 .. 1400.
	clips.push_back(Clip(1, 0, 600, 2.0, AUDIO_KEEP_PITCH));   // 1400 .. 1700 (twice as fast: half as long).
	list.set(clips);
	// Without any, every seam is a cut: the start, the two joins and the end.
	CHECK(list.transitions().size() == 4);
	CHECK(!list.has_transitions() && list.transitions_valid());
	CHECK(list.fade_level(0) == 1.0 && list.fade_level(500) == 1.0);
	// The most a seam may last is half of the shorter clip either side.
	CHECK(list.max_transition_ticks(0) == 500);
	CHECK(list.max_transition_ticks(1) == 200);
	CHECK(list.max_transition_ticks(2) == 150);
	CHECK(list.max_transition_ticks(3) == 150);
	CHECK(list.max_transition_ticks(4) == 0 && list.max_transition_ticks(-1) == 0);

	std::vector<Transition> transitions(4);
	transitions[0] = Transition(TRANSITION_FADE, 400);
	transitions[3] = Transition(TRANSITION_FADE, 100);
	list.set(clips, transitions);
	CHECK(list.has_transitions() && list.transitions_valid());
	CHECK(list.transition(0) == transitions[0] && list.transition(3) == transitions[3] && list.transition(1).is_cut());
	CHECK(list.transition(9).is_cut());
	// The picture comes up from black over the first 400 and goes down to black over the last 100.
	CHECK_NEAR(list.fade_level(0), 0.0);
	CHECK_NEAR(list.fade_level(100), 0.25);
	CHECK_NEAR(list.fade_level(400), 1.0);
	CHECK_NEAR(list.fade_level(1000), 1.0);
	CHECK_NEAR(list.fade_level(1600), 1.0);
	CHECK_NEAR(list.fade_level(1650), 0.5);
	CHECK_NEAR(list.fade_level(1700), 0.0);
	CHECK_NEAR(list.fade_level(-5), 0.0);
	// A clip that is both first and last (the only one) takes a fade at each end, each at most half of it.
	EditList single;
	std::vector<Clip> one;
	one.push_back(Clip(0, 0, 1000));
	std::vector<Transition> both(2);
	both[0] = Transition(TRANSITION_FADE, 500);
	both[1] = Transition(TRANSITION_FADE, 500);
	single.set(one, both);
	CHECK(single.transitions_valid());
	CHECK_NEAR(single.fade_level(250), 0.5);
	CHECK_NEAR(single.fade_level(500), 1.0);
	CHECK_NEAR(single.fade_level(750), 0.5);

	// What is not allowed: too long, a fade at a join, a crossfade at an end or with no length, a cut that lasts, a negative one, the wrong number.
	transitions[0] = Transition(TRANSITION_FADE, 501);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	transitions[0] = Transition(TRANSITION_FADE, 400);
	transitions[1] = Transition(TRANSITION_FADE, 100);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	transitions[1] = Transition(TRANSITION_CROSSFADE, 0);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	transitions[1] = Transition();
	transitions[0] = Transition(TRANSITION_CROSSFADE, 100);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	transitions[0] = Transition(TRANSITION_FADE, 400);
	transitions[1] = Transition(TRANSITION_CUT, 50);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	transitions[1] = Transition();
	transitions[3] = Transition(TRANSITION_FADE, -1);
	list.set(clips, transitions);
	CHECK(!list.transitions_valid());
	list.set(clips, std::vector<Transition>(2));     // The wrong number: all cuts.
	CHECK(list.transitions().size() == 4 && !list.has_transitions());
	list.set(std::vector<Clip>());
	CHECK(list.transitions().empty() && list.transitions_valid());

	// Through the model, which refuses what is not valid and keeps the transitions with the clips (and through the source view).
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	std::vector<Clip> model_clips;
	model_clips.push_back(Clip(0, S(0), S(50)));
	model_clips.push_back(Clip(1, S(0), S(40)));
	std::vector<Transition> model_transitions(3);
	model_transitions[0] = Transition(TRANSITION_FADE, S(10));
	model_transitions[2] = Transition(TRANSITION_FADE, S(20));
	CHECK(model.set_edit(model_clips, model_transitions));
	CHECK(model.edit_list().transition(0) == model_transitions[0] && model.edit_list().transition(2) == model_transitions[2]);
	model_transitions[2] = Transition(TRANSITION_FADE, S(21));       // Over half of the 40 seconds.
	CHECK(!model.set_edit(model_clips, model_transitions));
	CHECK(model.edit_list().transition(2).ticks == S(20));           // Unchanged.
	CHECK(model.set_clips(model_clips));                             // The plain way: all cuts.
	CHECK(!model.edit_list().has_transitions());
	model_transitions[2] = Transition(TRANSITION_FADE, S(20));
	CHECK(model.set_edit(model_clips, model_transitions));
	model.begin_source_view();
	CHECK(!model.edit_list().has_transitions());                     // The files whole, with nothing between them.
	CHECK(model.transitions_to_keep().size() == 3 && model.transitions_to_keep()[0].ticks == S(10));
	model.end_source_view();
	CHECK(model.edit_list().transition(0).ticks == S(10) && model.edit_list().transition(2).ticks == S(20));
}

// Crossfades: the clips overlap, so the timeline is shorter, and what shows at a time is one clip or a mix of two.
static void test_crossfades() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 1000));      // A: 1000 long.
	clips.push_back(Clip(1, 0, 600));       // B: 600 long.
	clips.push_back(Clip(0, 2000, 3000));   // C: 1000 long.
	std::vector<Transition> transitions(4);
	transitions[1] = Transition(TRANSITION_CROSSFADE, 200);     // A|B: B starts 200 before A ends.
	transitions[2] = Transition(TRANSITION_CROSSFADE, 300);     // B|C: the most B allows is 300.
	EditList list;
	list.set(clips, transitions);
	CHECK(list.transitions_valid());
	CHECK(list.start(0) == 0 && list.start(1) == 800 && list.start(2) == 1100);
	CHECK(list.end(0) == 1000 && list.end(1) == 1400 && list.end(2) == 2100);
	CHECK(list.total_ticks() == 2100);                           // 1000 + 600 + 1000 - 200 - 300.
	CHECK(list.overlap_ticks(1) == 200 && list.overlap_ticks(2) == 300 && list.overlap_ticks(0) == 0 && list.overlap_ticks(3) == 0);
	// Too long (over half of the shorter clip) is not valid, and the starts don't go wrong all the same.
	transitions[2] = Transition(TRANSITION_CROSSFADE, 301);
	EditList too_long;
	too_long.set(clips, transitions);
	CHECK(!too_long.transitions_valid());
	CHECK(too_long.start(2) == 1100);                            // Kept to the 300 that fits.
	transitions[2] = Transition(TRANSITION_CROSSFADE, 300);

	CHECK(list.has_crossfades());
	{
		EditList plain;
		plain.set(list.clips());
		CHECK(!plain.has_crossfades());
	}

	// The clip a time belongs to is the one that shows more of itself: the switch is half way through each overlap.
	CHECK(list.clip_index_for(0) == 0 && list.clip_index_for(899) == 0);
	CHECK(list.clip_index_for(900) == 1 && list.clip_index_for(1000) == 1 && list.clip_index_for(1099) == 1);
	CHECK(list.clip_index_for(1100) == 1 && list.clip_index_for(1249) == 1);
	CHECK(list.clip_index_for(1250) == 2 && list.clip_index_for(2100) == 2 && list.clip_index_for(99999) == 2);
	CHECK(list.clip_index_for(-5) == 0);

	// What shows.
	EditList::Mix mix = list.mix_at(500);
	CHECK(mix.first == 0 && mix.second == -1);
	mix = list.mix_at(800);
	CHECK(mix.first == 0 && mix.second == 1);
	CHECK_NEAR(mix.weight, 0.0);
	mix = list.mix_at(900);
	CHECK(mix.first == 0 && mix.second == 1);
	CHECK_NEAR(mix.weight, 0.5);
	mix = list.mix_at(999);
	CHECK(mix.first == 0 && mix.second == 1);
	CHECK_NEAR(mix.weight, 0.995);
	mix = list.mix_at(1000);                                     // A is over; B alone, until the overlap with C.
	CHECK(mix.first == 1 && mix.second == -1);
	mix = list.mix_at(1100);
	CHECK(mix.first == 1 && mix.second == 2);
	CHECK_NEAR(mix.weight, 0.0);
	mix = list.mix_at(1250);
	CHECK(mix.first == 1 && mix.second == 2);
	CHECK_NEAR(mix.weight, 0.5);
	mix = list.mix_at(1400);
	CHECK(mix.first == 2 && mix.second == -1);
	mix = list.mix_at(2100);
	CHECK(mix.first == 2 && mix.second == -1);

	// Where in the footage each clip is: each uses its own, in the overlap as anywhere (A's end and B's start are played together).
	CHECK(list.footage_for_clip_at(0, 900) == 900);
	CHECK(list.footage_for_clip_at(1, 900) == list.footage_start(1) + 100);
	CHECK(list.footage_for_clip_at(1, 0) == list.footage_start(1));                  // Before its start: its first frame.
	CHECK(list.footage_for_clip_at(0, 5000) == list.footage_start(0) + 1000);        // After its end: its end.
	CHECK(list.footage_for_timeline(900) == list.footage_for_clip_at(1, 900));      // The one the time belongs to.
	CHECK(list.footage_for_timeline(850) == list.footage_for_clip_at(0, 850));
	FileTime source = list.source_for(900);
	CHECK(source.media == 1 && source.local_ticks == 100);
	CHECK(list.timeline_for(1, 100) == 900);
	CHECK(list.timeline_for_footage(list.footage_start(1) + 100) == 900);

	// At another speed the overlap is in timeline time, so a faster clip uses more footage in it.
	std::vector<Clip> speedy;
	speedy.push_back(Clip(0, 0, 1000));
	speedy.push_back(Clip(1, 0, 1200, 2.0, AUDIO_KEEP_PITCH));   // 600 on the timeline.
	std::vector<Transition> speedy_transitions(3);
	speedy_transitions[1] = Transition(TRANSITION_CROSSFADE, 200);
	EditList fast;
	fast.set(speedy, speedy_transitions);
	CHECK(fast.transitions_valid() && fast.start(1) == 800 && fast.total_ticks() == 1400);
	CHECK(fast.footage_for_clip_at(1, 1000) == fast.footage_start(1) + 400);        // 200 timeline ticks in at 2x.
	CHECK(fast.footage_for_clip_at(0, 1000) == 1000);

	// No crossfades at all: nothing is different from before (clips butt up, a time belongs to the clip it is in).
	EditList plain;
	plain.set(clips);
	CHECK(plain.start(1) == 1000 && plain.total_ticks() == 2600);
	CHECK(plain.clip_index_for(999) == 0 && plain.clip_index_for(1000) == 1);
	CHECK(plain.mix_at(999).second == -1 && plain.mix_at(1000).second == -1);

	// Fades at the ends go by the shorter total.
	transitions[3] = Transition(TRANSITION_FADE, 400);
	list.set(clips, transitions);
	CHECK(list.transitions_valid());
	CHECK_NEAR(list.fade_level(2100 - 200), 0.5);
	CHECK_NEAR(list.fade_level(2100), 0.0);
}

// Joins that carry straight on from one piece of footage to the next.
static void test_continuous_joins() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	std::vector<Clip> clips;
	clips.push_back(Clip(0, S(0), S(30)));
	clips.push_back(Clip(0, S(30), S(100)));        // The same file straight on: a split.
	clips.push_back(Clip(1, S(0), S(40)));          // The end of file A is not what clip 1 ends at... A is 100 long, and clip 1 ends at 100.
	clips.push_back(Clip(3, S(0), S(50)));          // File B (40) ended at its end, but D is not next in capture order.
	clips.push_back(Clip(2, S(10), S(70)));         // Not from the start of C.
	CHECK(model.set_clips(clips));
	CHECK(!model.join_is_continuous(0) && !model.join_is_continuous(5));
	CHECK(model.join_is_continuous(1));
	CHECK(model.join_is_continuous(2));             // Whole file A to the start of B, the next in capture order.
	CHECK(!model.join_is_continuous(3));            // B's end to D: C is next.
	CHECK(!model.join_is_continuous(4));
	// A gap, and a different order of the files.
	clips[1] = Clip(0, S(31), S(100));
	CHECK(model.set_clips(clips));
	CHECK(!model.join_is_continuous(1));
	std::vector<int> order;
	order.push_back(0); order.push_back(1); order.push_back(3); order.push_back(2);
	CHECK(model.set_capture_order(order));
	clips[3] = Clip(3, S(0), S(50));
	clips[2] = Clip(1, S(0), S(40));
	CHECK(model.set_clips(clips));
	CHECK(model.join_is_continuous(3));             // B then D, now the next in capture order.
	// A clip that stops short of the end of its file is not continuous with what follows it (A to B still is).
	clips[2] = Clip(1, S(0), S(39));
	CHECK(model.set_clips(clips));
	CHECK(!model.join_is_continuous(3) && model.join_is_continuous(2));
}

int main() {
	test_transitions();
	test_continuous_joins();
	test_crossfades();
	printf(fails ? "FAILED\n" : "all ok\n");
	return fails;
}
