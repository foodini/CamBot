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

static std::vector<int> order(int a, int b, int c, int d) {
	std::vector<int> result;
	result.push_back(a); result.push_back(b); result.push_back(c); result.push_back(d);
	return result;
}

static void test_edit_list() {
	EditList list;
	CHECK(list.count() == 0 && list.total_ticks() == 0 && list.clip_index_for(5) == -1);
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 100));
	clips.push_back(Clip(1, 50, 80));
	clips.push_back(Clip(0, 100, 150));
	list.set(clips);
	CHECK(list.count() == 3 && list.total_ticks() == 180);
	CHECK(list.start(0) == 0 && list.start(1) == 100 && list.start(2) == 130);
	CHECK(list.clip_index_for(-5) == 0);
	CHECK(list.clip_index_for(99) == 0);
	CHECK(list.clip_index_for(100) == 1);    // A boundary belongs to the later clip.
	CHECK(list.clip_index_for(129) == 1);
	CHECK(list.clip_index_for(130) == 2);
	CHECK(list.clip_index_for(179) == 2);
	CHECK(list.clip_index_for(9999) == 2);
	CHECK(list.source_for(10) == FileTime(0, 10));
	CHECK(list.source_for(100) == FileTime(1, 50));
	CHECK(list.source_for(115) == FileTime(1, 65));
	CHECK(list.source_for(130) == FileTime(0, 100));
	CHECK(list.source_for(180) == FileTime(0, 150));   // The end.
	CHECK(list.source_for(5000) == FileTime(0, 150));
	CHECK(list.timeline_for(1, 65) == 115);
	CHECK(list.timeline_for(1, 10) == 100);   // Before the clip: clamped to its start.
	CHECK(list.timeline_for(1, 500) == 130);  // After it: clamped to its end.
}

static void test_capture_sequence() {
	CaptureSequence capture;
	std::vector<int64_t> lengths;
	lengths.push_back(100); lengths.push_back(40); lengths.push_back(70);
	CHECK(!CaptureSequence::is_valid_order(order(0, 1, 2, 3), 3));
	std::vector<int> bad;
	bad.push_back(0); bad.push_back(1); bad.push_back(1);
	CHECK(!capture.set(bad, lengths));
	bad[2] = 3;
	CHECK(!capture.set(bad, lengths));
	std::vector<int> good;
	good.push_back(0); good.push_back(2); good.push_back(1);   // A, C, B
	CHECK(capture.set(good, lengths));
	CHECK(capture.flight_start(0) == 0 && capture.flight_start(2) == 100 && capture.flight_start(1) == 170);
	CHECK(capture.total_ticks() == 210);
	CHECK(capture.flight_ticks(FileTime(1, 5)) == 175);
	CHECK(capture.file_time_for(0) == FileTime(0, 0));
	CHECK(capture.file_time_for(99) == FileTime(0, 99));
	CHECK(capture.file_time_for(100) == FileTime(2, 0));
	CHECK(capture.file_time_for(175) == FileTime(1, 5));
	CHECK(capture.file_time_for(210) == FileTime(1, 40));   // The end.
	CHECK(capture.file_time_for(-3) == FileTime(0, 0));
}

static void test_defaults_and_validation() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	CHECK(model.media_count() == 4);
	CHECK(model.edit_list().count() == 4 && model.edit_list().total_ticks() == S(310));
	CHECK(!model.edit_started());
	// Default clips follow the capture order.
	CHECK(model.set_capture_order(order(0, 2, 1, 3)));
	model.set_default_clips();
	CHECK(model.edit_list().clip(1).media == 2 && model.edit_list().clip(2).media == 1);
	// Bad clips are refused and change nothing.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, S(10)));
	CHECK(model.set_clips(clips) && model.edit_list().count() == 1);
	clips.push_back(Clip(7, 0, 10));
	CHECK(!model.set_clips(clips) && model.edit_list().count() == 1);
	clips.pop_back();
	clips.push_back(Clip(1, 0, S(41)));          // Longer than the file.
	CHECK(!model.set_clips(clips));
	clips.back() = Clip(1, S(5), S(5));          // Empty.
	CHECK(!model.set_clips(clips));
	clips.back() = Clip(1, S(6), S(5));          // Backwards.
	CHECK(!model.set_clips(clips));
	clips.back() = Clip(1, -1, S(5));
	CHECK(!model.set_clips(clips));
	CHECK(model.edit_list().count() == 1);
	CHECK(!model.set_capture_order(order(0, 0, 1, 2)));
	CHECK(model.seconds_to_ticks(1.5) == 1500);
}

static void test_sync_math() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	// No anchors: the clocks are taken to agree.
	CHECK_NEAR(model.telemetry_seconds(50.0), 50.0);
	CHECK_NEAR(model.offset(), 0.0);
	// The plain-offset controls make one anchor at the start of the flight.
	model.set_offset(12.5);
	CHECK(model.anchors().size() == 1);
	CHECK(model.anchors()[0].at == FileTime(0, 0));
	CHECK_NEAR(model.offset(), 12.5);
	CHECK_NEAR(model.telemetry_seconds(100.0), 87.5);
	model.add_to_offset(-0.5);
	CHECK_NEAR(model.offset(), 12.0);
	model.set_offset(-3.0);
	CHECK(model.anchors().size() == 1);
	CHECK_NEAR(model.telemetry_seconds(10.0), 13.0);

	// Two anchors: the offset runs in a straight line between them and is held outside them.
	std::vector<SyncAnchor> anchors;
	anchors.push_back(SyncAnchor(FileTime(0, S(10)), 10.0f - 20.0f));    // flight 10, offset +20
	anchors.push_back(SyncAnchor(FileTime(3, S(50)), 260.0f - 40.0f));   // flight 260, offset +40
	model.set_anchors(anchors);
	CHECK_NEAR(model.offset_at(0.0), 20.0);
	CHECK_NEAR(model.offset_at(10.0), 20.0);
	CHECK_NEAR(model.offset_at(135.0), 30.0);
	CHECK_NEAR(model.offset_at(260.0), 40.0);
	CHECK_NEAR(model.offset_at(300.0), 40.0);
	CHECK_NEAR(model.telemetry_seconds(135.0), 105.0);
	CHECK_NEAR(model.offset(), 20.0);
	// Moving both together keeps the shape.
	model.set_offset(25.0);
	CHECK_NEAR(model.offset_at(10.0), 25.0);
	CHECK_NEAR(model.offset_at(260.0), 45.0);
	// Anchors may be given in any order.
	std::vector<SyncAnchor> reversed;
	reversed.push_back(anchors[1]); reversed.push_back(anchors[0]);
	model.set_anchors(reversed);
	CHECK_NEAR(model.offset_at(135.0), 30.0);
	// An anchor naming a missing file is ignored.
	reversed.push_back(SyncAnchor(FileTime(9, 0), 1.0f));
	model.set_anchors(reversed);
	CHECK(model.anchors().size() == 2);
}

// Ron's scenario: the files were named A, B, C, D but recorded in the order A, C, B, D. The
// user syncs with the wrong order, edits, then fixes the order in the sync tool. The right
// telemetry must show on every clip afterward, with nothing re-confirmed per clip.
static void test_reorder_after_syncing_and_editing() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);    // Index = name order: A B C D.

	// Truth: telemetry time = true flight time + 100, where true flight order is A, C, B, D.
	// Launch is 10 s into A, landing 50 s into D. Both land at the same flight time whichever
	// way B and C are ordered, so the user can set them without noticing the files are wrong.
	std::vector<SyncAnchor> anchors;
	anchors.push_back(SyncAnchor(FileTime(0, S(10)), 10.0f + 100.0f));
	anchors.push_back(SyncAnchor(FileTime(3, S(50)), 260.0f + 100.0f));
	model.set_anchors(anchors);

	// The user cuts a few clips.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, S(20), S(30)));      // A
	clips.push_back(Clip(1, S(0), S(20)));       // B
	clips.push_back(Clip(2, S(10), S(40)));      // C
	clips.push_back(Clip(1, S(30), S(40)));      // B again
	clips.push_back(Clip(3, S(60), S(90)));      // D
	CHECK(model.set_clips(clips));
	model.set_edit_started(true);

	// With the order wrong, B's frame at 5 s shows telemetry for flight time 105 (+100).
	CHECK_NEAR(model.telemetry_seconds(model.flight_seconds(FileTime(1, S(5)))), 105.0 + 100.0);

	EditModel fixed = model;
	CHECK(fixed.set_capture_order(order(0, 2, 1, 3)));
	// Now B's 5 s frame is 5 s into the file that follows C: true flight time 175.
	CHECK_NEAR(fixed.telemetry_seconds(fixed.flight_seconds(FileTime(1, S(5)))), 175.0 + 100.0);
	CHECK_NEAR(fixed.telemetry_seconds(fixed.flight_seconds(FileTime(2, S(5)))), 105.0 + 100.0);
	// A and D did not move, and the anchors still hold exactly.
	CHECK_NEAR(fixed.telemetry_seconds(fixed.flight_seconds(FileTime(0, S(10)))), 110.0);
	CHECK_NEAR(fixed.telemetry_seconds(fixed.flight_seconds(FileTime(3, S(50)))), 360.0);
	// Clips look the telemetry up through their file, wherever they sit on the timeline.
	CHECK_NEAR(fixed.telemetry_seconds_for_timeline(S(10)), 170.0 + 100.0);   // Start of the first B clip.
	CHECK_NEAR(fixed.telemetry_seconds_for_timeline(S(40)), 120.0 + 100.0);   // 10 s into the C clip, which starts 10 s into C.
	// The warning counts the clips that would change: both B clips and the C clip, not A or D.
	CHECK(EditModel::clips_with_changed_telemetry(model, fixed) == 3);
	// A change that touches nothing the clips show reports none.
	EditModel same = model;
	same.set_capture_order(order(0, 1, 2, 3));
	CHECK(EditModel::clips_with_changed_telemetry(model, same) == 0);
	// And the original model is untouched, so a cancelled change leaves everything as it was.
	CHECK(model.capture().order() == order(0, 1, 2, 3));
}

static void test_reorder_with_drift() {
	// A camera clock that runs fast: offset +0 at the start of A, +10 s at the end of D. Which
	// files lie between the anchors decides how much of that drift each file gets.
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	std::vector<SyncAnchor> anchors;
	anchors.push_back(SyncAnchor(FileTime(0, 0), 0.0f));                     // flight 0, offset 0
	anchors.push_back(SyncAnchor(FileTime(3, S(100)), 310.0f - 10.0f));      // flight 310, offset 10
	model.set_anchors(anchors);
	// B starts at flight 100: offset = 10 * 100/310.
	CHECK_NEAR(model.offset_at(model.flight_seconds(FileTime(1, 0))), 10.0 * 100.0 / 310.0);
	CHECK(model.set_capture_order(order(0, 2, 1, 3)));
	// Now B starts at flight 170.
	CHECK_NEAR(model.offset_at(model.flight_seconds(FileTime(1, 0))), 10.0 * 170.0 / 310.0);
	// The ends are unchanged.
	CHECK_NEAR(model.offset_at(0.0), 0.0);
	CHECK_NEAR(model.offset_at(310.0), 10.0);
}

static void test_launch_mark_follows_its_file() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	model.set_launch_flight_seconds(120.0);          // 20 s into B
	CHECK(model.launch_mark() == FileTime(1, S(20)));
	CHECK_NEAR(model.launch_flight_seconds(), 120.0);
	model.set_capture_order(order(0, 2, 1, 3));      // B now comes after C.
	CHECK(model.launch_mark() == FileTime(1, S(20)));
	CHECK_NEAR(model.launch_flight_seconds(), 190.0);
}

static void test_timeline_to_flight() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);
	std::vector<Clip> clips;
	clips.push_back(Clip(3, S(0), S(10)));
	clips.push_back(Clip(0, S(20), S(30)));
	CHECK(model.set_clips(clips));
	CHECK_NEAR(model.flight_seconds_for_timeline(S(5)), 210.0 + 5.0);
	CHECK_NEAR(model.flight_seconds_for_timeline(S(10)), 20.0);        // First frame of the second clip.
	CHECK_NEAR(model.flight_seconds_for_timeline(S(14)), 24.0);
	CHECK_NEAR(model.flight_seconds_for_timeline(S(500)), 30.0);       // Past the end: the last moment.
	EditModel empty;
	CHECK_NEAR(empty.flight_seconds_for_timeline(5), 0.0);
}

static void test_used_spans() {
	EditModel model;
	model.set_media(names4(), durations(100, 40, 70, 100), SPT);    // A B C D start at 0, 100, 140, 210 seconds.
	double start = -1.0, end = -1.0;
	// Whole files: the whole flight.
	CHECK(model.used_flight_span(&start, &end));
	CHECK_NEAR(start, 0.0);
	CHECK_NEAR(end, 310.0);

	// A piece of D first, then a piece of A: the span covers both, whichever is first on the timeline.
	std::vector<Clip> clips;
	clips.push_back(Clip(3, S(0), S(10)));
	clips.push_back(Clip(0, S(20), S(30)));
	CHECK(model.set_clips(clips));
	CHECK(model.used_flight_span(&start, &end));
	CHECK_NEAR(start, 20.0);
	CHECK_NEAR(end, 220.0);

	// Telemetry time is that, less the sync offset (here the telemetry started 5 seconds after the camera).
	model.set_offset(5.0);
	CHECK(model.used_telemetry_span(&start, &end));
	CHECK_NEAR(start, 15.0);
	CHECK_NEAR(end, 215.0);

	// Trimming the front of the first clip moves the start (the telemetry before it is no longer in the span).
	std::vector<Clip> trimmed;
	trimmed.push_back(Clip(0, S(50), S(100)));
	trimmed.push_back(Clip(1, S(0), S(40)));
	CHECK(model.set_clips(trimmed));
	CHECK(model.used_telemetry_span(&start, &end));
	CHECK_NEAR(start, 45.0);
	CHECK_NEAR(end, 135.0);

	// Nothing to span with no clips.
	EditModel empty;
	CHECK(!empty.used_flight_span(&start, &end));
	CHECK(!empty.used_telemetry_span(&start, &end));
	CHECK_NEAR(start, 45.0);    // Left alone.
}

int main() {
	test_edit_list();
	test_capture_sequence();
	test_defaults_and_validation();
	test_sync_math();
	test_reorder_after_syncing_and_editing();
	test_reorder_with_drift();
	test_launch_mark_follows_its_file();
	test_timeline_to_flight();
	test_used_spans();
	printf(fails ? "FAILED\n" : "all ok\n");
	return fails;
}
