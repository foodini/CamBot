// Clips played at other speeds: the arithmetic of the edit list (edit_model.h), the cuts that carry a speed along (timeline_edit.h),
// the sound plan (audio_plan.h) and the video decoder showing the right frame for a time on the timeline (against fake_ffmpeg.cpp).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "audio_plan.h"
#include "edit_model.h"
#include "fake_ffmpeg.h"
#include "media_decoder.h"
#include "timeline_edit.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)
#define CHECK_MSG(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

static const int FT = 30;   // Ticks per frame in the fake files.

// ---------------------------------------------------------------------------------------------
// The edit list
// ---------------------------------------------------------------------------------------------

static std::vector<Clip> sample_clips() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 3000));                          // Normal: 3000 on the timeline.
	clips.push_back(Clip(0, 3000, 6000, 0.5, AUDIO_MUTE));      // Half speed: 6000.
	clips.push_back(Clip(1, 0, 4000, 2.0, AUDIO_KEEP_PITCH));   // Double: 2000.
	return clips;
}

static void test_edit_list_arithmetic() {
	EditList list;
	list.set(sample_clips());
	CHECK(list.has_speed_changes());
	CHECK(list.start(0) == 0 && list.start(1) == 3000 && list.start(2) == 9000);
	CHECK(list.total_ticks() == 11000);
	CHECK(list.footage_start(0) == 0 && list.footage_start(1) == 3000 && list.footage_start(2) == 6000);
	CHECK(list.footage_total() == 10000);
	CHECK(list.clip(1).timeline_ticks() == 6000 && list.clip(1).duration_ticks() == 3000);

	// A moment in the half-speed clip: 1500 into it on the timeline is 750 of footage.
	FileTime at = list.source_for(4500);
	CHECK(at.media == 0 && at.local_ticks == 3750);
	CHECK(list.timeline_for(1, 3750) == 4500);
	CHECK(list.footage_for_timeline(4500) == 3750);
	CHECK(list.timeline_for_footage(3750) == 4500);
	// The double-speed clip: 100 into it on the timeline is 200 of the file.
	at = list.source_for(9100);
	CHECK(at.media == 1 && at.local_ticks == 200);
	CHECK(list.timeline_for(2, 200) == 9100);
	// The very end.
	CHECK(list.source_for(11000).local_ticks == 4000);
	CHECK(list.timeline_for(2, 4000) == 11000);
	CHECK(list.timeline_for_footage(10000) == 11000);
	CHECK(list.footage_for_timeline(11000) == 10000);
	// Footage times name the clip they are in.
	CHECK(list.clip_index_for_footage(0) == 0 && list.clip_index_for_footage(2999) == 0);
	CHECK(list.clip_index_for_footage(3000) == 1 && list.clip_index_for_footage(5999) == 1);
	CHECK(list.clip_index_for_footage(6000) == 2);
	CHECK(list.source_for_footage(6100).media == 1 && list.source_for_footage(6100).local_ticks == 100);
	CHECK(list.footage_for(1, 4000) == 4000 && list.footage_for(2, 100) == 6100);
	// Out of range times are kept inside.
	CHECK(list.timeline_for(1, 0) == 3000);
	CHECK(list.timeline_for(1, 9999) == 9000);

	// Without speed changes the two kinds of time are the same, tick for tick.
	std::vector<Clip> plain;
	plain.push_back(Clip(0, 100, 900));
	plain.push_back(Clip(1, 0, 500));
	EditList flat;
	flat.set(plain);
	CHECK(!flat.has_speed_changes());
	CHECK(flat.total_ticks() == flat.footage_total() && flat.total_ticks() == 1300);
	for (int64_t t = 0; t <= 1300; t += 7)
		CHECK(flat.footage_for_timeline(t) == t && flat.timeline_for_footage(t) == t);
}

static void test_round_trips_with_awkward_speeds() {
	// A frame's time carried to the timeline and back lands on that frame, or within a tick or two of it.
	double speeds[] = { 0.1, 0.3, 0.77, 1.3, 1.7, 3.0, 7.9, 16.0 };
	for (size_t s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
		std::vector<Clip> clips;
		clips.push_back(Clip(0, 0, 1000));
		clips.push_back(Clip(0, 1000, 2000 * 30, speeds[s], AUDIO_KEEP_PITCH));
		clips.push_back(Clip(0, 0, 1000));
		EditList list;
		list.set(clips);
		for (int64_t f = 0; f < list.footage_total(); f += 30) {
			const int64_t t = list.timeline_for_footage(f);
			const int64_t back = list.footage_for_timeline(t);
			const int64_t tolerance = (int64_t)ceil(speeds[s]) + 1;
			CHECK_MSG(llabs((long long)(back - f)) <= tolerance, "speed %g footage %lld -> %lld -> %lld", speeds[s], (long long)f, (long long)t, (long long)back);
		}
		// Time only moves forward.
		int64_t last = -1;
		for (int64_t f = 0; f < list.footage_total(); f += 13) {
			const int64_t t = list.timeline_for_footage(f);
			CHECK(t >= last);
			last = t;
		}
	}
}

static void test_model_checks_speeds() {
	EditModel model;
	std::vector<std::string> paths;
	paths.push_back("a.mp4");
	std::vector<int64_t> durations;
	durations.push_back(10000);
	model.set_media(paths, durations, 1.0 / 30000.0);
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 5000, 0.25, AUDIO_CHANGE_PITCH));
	CHECK(model.set_clips(clips));
	CHECK(model.edit_list().total_ticks() == 20000);
	clips[0].speed = 0.05;
	CHECK(!model.set_clips(clips));
	clips[0].speed = 16.5;
	CHECK(!model.set_clips(clips));
	clips[0].speed = 16.0;
	CHECK(model.set_clips(clips));
	clips[0].audio_mode = 3;
	CHECK(!model.set_clips(clips));
	clips[0].audio_mode = AUDIO_MUTE;
	CHECK(model.set_clips(clips));
	CHECK(model.edit_list().clip(0).audio_mode == AUDIO_MUTE);

	// Telemetry follows the footage: twice as fast moves through the flight twice as fast.
	clips[0] = Clip(0, 0, 6000, 2.0, AUDIO_KEEP_PITCH);
	CHECK(model.set_clips(clips));
	CHECK(fabs(model.flight_seconds_for_timeline(1000) - model.ticks_to_seconds(2000)) < 1e-9);
	CHECK(model.edit_list().total_ticks() == 3000);
	// Equality includes the speed and the sound.
	CHECK(Clip(0, 0, 10) == Clip(0, 0, 10, 1.0, AUDIO_KEEP_PITCH));
	CHECK(Clip(0, 0, 10) != Clip(0, 0, 10, 2.0, AUDIO_KEEP_PITCH));
	CHECK(Clip(0, 0, 10) != Clip(0, 0, 10, 1.0, AUDIO_MUTE));
}

// ---------------------------------------------------------------------------------------------
// The cuts
// ---------------------------------------------------------------------------------------------

static void test_cuts_carry_the_speed() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 3000));
	clips.push_back(Clip(0, 3000, 6000, 0.5, AUDIO_CHANGE_PITCH));
	clips.push_back(Clip(1, 0, 4000, 2.0, AUDIO_MUTE));
	EditList list;
	list.set(clips);
	const int64_t frame = 30;

	// Split the half-speed clip at timeline 4500 (3750 of the file).
	timeline_edit::Result split = timeline_edit::split(list, 4500, frame);
	CHECK(split.ok && split.clips.size() == 4);
	CHECK(split.clips[1].in_ticks == 3000 && split.clips[1].out_ticks == 3750 && split.clips[1].speed == 0.5 && split.clips[1].audio_mode == AUDIO_CHANGE_PITCH);
	CHECK(split.clips[2].in_ticks == 3750 && split.clips[2].out_ticks == 6000 && split.clips[2].speed == 0.5 && split.clips[2].audio_mode == AUDIO_CHANGE_PITCH);
	CHECK(split.clips[0].speed == 1.0 && split.clips[3].speed == 2.0);
	EditList after;
	after.set(split.clips);
	CHECK(after.total_ticks() == list.total_ticks());                // Nothing lost.
	CHECK(split.playhead_ticks == 4500 && after.start(2) == 4500);   // On the first frame of the second half.

	// Trim left: the playhead's frame becomes the first; what follows closes up.
	timeline_edit::Result trimmed = timeline_edit::trim_left(list, 4500, frame);
	CHECK(trimmed.ok && trimmed.clips[1].in_ticks == 3750 && trimmed.clips[1].speed == 0.5);
	CHECK(trimmed.playhead_ticks == 3000);
	// Trim right at the same place: it keeps the frame under the playhead.
	timeline_edit::Result cut = timeline_edit::trim_right(list, 4500, frame);
	CHECK(cut.ok && cut.clips[1].out_ticks == 3780 && cut.clips[1].speed == 0.5);
	after.set(cut.clips);
	CHECK(after.clip(1).timeline_ticks() == 1560);   // 780 of file at half speed.
	CHECK(cut.playhead_ticks == 4500);

	// Removing a clip takes out its timeline length, not its footage.
	timeline_edit::Result removed = timeline_edit::remove_clip(list, 9500, frame);
	CHECK(removed.ok && removed.clips.size() == 2);
	CHECK(removed.playhead_ticks == 9000 - 30 || removed.playhead_ticks == 8970);   // The last frame there is now.
	removed = timeline_edit::remove_clip(list, 4500, frame);
	CHECK(removed.ok && removed.playhead_ticks == 3000);

	// Moving a clip keeps the playhead on the same frame of the same footage.
	timeline_edit::Result moved = timeline_edit::move_clip(list, 1, 0, 4500);
	CHECK(moved.ok && moved.clips[0].speed == 0.5 && moved.clips[1].speed == 1.0);
	CHECK(moved.playhead_ticks == 1500);     // Same 1500 into the half-speed clip, which now starts at 0.
	moved = timeline_edit::move_clip(list, 2, 0, 9200);
	CHECK(moved.ok && moved.clips[0].speed == 2.0 && moved.playhead_ticks == 200);   // 200 into a double-speed clip is 400 of the file.

	// Dropping works on lengths on the timeline: the half-speed clip is centred at 3000 + 3000 = 6000.
	CHECK(timeline_edit::drop_index(list, 0, 5900.0) == 0);
	CHECK(timeline_edit::drop_index(list, 0, 6100.0) == 1);
	CHECK(timeline_edit::drop_marker_ticks(list, 2, 2) == 9000);
}

static void test_set_speed_edit() {
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 3000));
	clips.push_back(Clip(0, 3000, 6000));
	clips.push_back(Clip(1, 0, 4000));
	EditList list;
	list.set(clips);
	const int64_t frame = 30;

	// Half speed for the middle clip, with the playhead 600 into it (a frame start).
	CHECK(timeline_edit::can_set_speed(list, 3600, 0.5, AUDIO_KEEP_PITCH));
	timeline_edit::Result slowed = timeline_edit::set_speed(list, 3600, 0.5, AUDIO_CHANGE_PITCH, frame);
	CHECK(slowed.ok && slowed.clips.size() == 3);
	CHECK(slowed.clips[1].speed == 0.5 && slowed.clips[1].audio_mode == AUDIO_CHANGE_PITCH);
	CHECK(slowed.clips[0].speed == 1.0 && slowed.clips[2].speed == 1.0);
	CHECK(slowed.clips[1].in_ticks == 3000 && slowed.clips[1].out_ticks == 6000);   // The footage used is unchanged.
	EditList after;
	after.set(slowed.clips);
	CHECK(after.total_ticks() == 3000 + 6000 + 4000);
	CHECK(slowed.playhead_ticks == 3000 + 1200);         // Still on the frame 600 into the footage of that clip.
	CHECK(after.source_for(slowed.playhead_ticks).local_ticks == 3600);

	// The same again changes nothing; so does an impossible speed, or a time with no clip.
	CHECK(!timeline_edit::can_set_speed(list, 3600, 1.0, AUDIO_KEEP_PITCH));
	CHECK(!timeline_edit::can_set_speed(list, 3600, 0.05, AUDIO_KEEP_PITCH));
	CHECK(!timeline_edit::can_set_speed(list, 3600, 17.0, AUDIO_KEEP_PITCH));
	CHECK(!timeline_edit::can_set_speed(list, 3600, 2.0, 7));
	CHECK(!timeline_edit::set_speed(list, 3600, 0.05, AUDIO_KEEP_PITCH, frame).ok);
	CHECK(!timeline_edit::can_set_speed(after, 3000 + 1200, 0.5, AUDIO_CHANGE_PITCH));
	CHECK(timeline_edit::can_set_speed(after, 3000 + 1200, 0.5, AUDIO_MUTE));      // Only the sound.
	timeline_edit::Result muted = timeline_edit::set_speed(after, 3000 + 1200, 0.5, AUDIO_MUTE, frame);
	CHECK(muted.ok && muted.playhead_ticks == 3000 + 1200 && muted.clips[1].audio_mode == AUDIO_MUTE);
	CHECK(!timeline_edit::can_set_speed(EditList(), 0, 2.0, AUDIO_KEEP_PITCH));

	// Back to normal puts the playhead back where it was, and the clips the same as at first.
	timeline_edit::Result normal = timeline_edit::set_speed(after, slowed.playhead_ticks, 1.0, AUDIO_KEEP_PITCH, frame);
	CHECK(normal.ok && normal.playhead_ticks == 3600);
	CHECK(normal.clips[1] == clips[1]);

	// Faster: the clip is shorter and what follows closes up; the playhead keeps its frame.
	timeline_edit::Result quick = timeline_edit::set_speed(list, 3600, 4.0, AUDIO_KEEP_PITCH, frame);
	CHECK(quick.ok);
	after.set(quick.clips);
	CHECK(after.total_ticks() == 3000 + 750 + 4000 && after.start(2) == 3750);
	CHECK(quick.playhead_ticks == 3000 + 150);
	// With the speed set and then split, both halves keep it (see test_cuts_carry_the_speed), and the whole is as long.
	timeline_edit::Result halves = timeline_edit::split(after, quick.playhead_ticks + 60, frame);
	CHECK(halves.ok && halves.clips[1].speed == 4.0 && halves.clips[2].speed == 4.0);
}

// ---------------------------------------------------------------------------------------------
// The sound plan
// ---------------------------------------------------------------------------------------------

static void test_audio_plan_carries_speed() {
	const double TICK = 1.0 / 48000.0;   // One sample to a tick, to keep the numbers plain.
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 4800));
	clips.push_back(Clip(0, 4800, 9600, 2.0, AUDIO_KEEP_PITCH));
	clips.push_back(Clip(0, 9600, 12000, 2.0, AUDIO_KEEP_PITCH));          // Straight on, same speed, but stretched afresh.
	clips.push_back(Clip(0, 12000, 14000, 2.0, AUDIO_CHANGE_PITCH));       // Different treatment.
	clips.push_back(Clip(0, 14000, 16000, 2.0, AUDIO_CHANGE_PITCH));      // Straight on and sampled the same way: no cut.
	clips.push_back(Clip(0, 16000, 17000, 1.0, AUDIO_CHANGE_PITCH));      // Back to normal.
	EditList list;
	list.set(clips);
	std::vector<audio_plan::Segment> segments;
	audio_plan::plan(list, TICK, &segments);
	CHECK(segments.size() == 6);
	CHECK(segments[0].speed == 1.0 && segments[1].speed == 2.0 && segments[1].mode == AUDIO_KEEP_PITCH);
	CHECK(segments[1].timeline_start == 4800 && segments[1].length == 2400 && segments[1].source_start == 4800);
	CHECK(segments[2].timeline_start == 7200 && segments[2].length == 1200);
	CHECK(segments[2].source_start == 9600);
	// The segments still tile the timeline.
	for (size_t i = 1; i < segments.size(); i++)
		CHECK(segments[i].timeline_start == segments[i - 1].timeline_end());
	CHECK(segments.back().timeline_end() == list.total_ticks());
	// Fades: a change of speed is a cut; a kept-pitch stretch is made afresh each clip so it is a cut too; two clips sampled
	// the same way carry straight on.
	CHECK(segments[0].fade_out && segments[1].fade_in);
	CHECK(segments[1].fade_out && segments[2].fade_in);
	CHECK(segments[2].fade_out && segments[3].fade_in);
	CHECK(!segments[3].fade_out && !segments[4].fade_in);
	CHECK(segments[4].fade_out && segments[5].fade_in);
}

// ---------------------------------------------------------------------------------------------
// The decoder
// ---------------------------------------------------------------------------------------------

struct Setup {
	std::vector<std::string> paths;
	std::vector<int> frames;
};

static Setup make_setup(int count, int frames_each) {
	fake_ffmpeg_reset();
	Setup s;
	for (int i = 0; i < count; i++) {
		FakeFileSpec spec;
		spec.frames = frames_each; spec.gop = 10; spec.delay = 2; spec.tag = i; spec.audio = false;
		std::string path = "file" + std::to_string(i) + ".mp4";
		fake_ffmpeg_add_file(path, spec);
		s.paths.push_back(path);
		s.frames.push_back(frames_each);
	}
	return s;
}

// The frame number within its file, and the file, of the picture on screen.
static void shown(const MediaDecoder& decoder, int* file, int* frame) {
	const AVFrame* f = decoder.frame();
	*frame = f->data[0][0] | (f->data[0][1] << 8);
	*file = f->data[1][0];
}

// The frame the edit should show at timeline time t: the last one that begins at or before the moment of the file there.
static void expected_frame(const EditList& list, int64_t t, int* file, int* frame) {
	const FileTime source = list.source_for(t);
	*file = source.media;
	*frame = (int)(source.local_ticks / FT);
}

static void test_decoder_shows_the_right_frames() {
	Setup s = make_setup(2, 100);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));

	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 20 * FT));                       // 20 frames at normal speed: 600 ticks.
	clips.push_back(Clip(0, 20 * FT, 40 * FT, 0.5, AUDIO_KEEP_PITCH));   // 20 frames, each shown twice as long: 1200.
	clips.push_back(Clip(1, 10 * FT, 50 * FT, 2.0, AUDIO_KEEP_PITCH));   // 40 frames at double speed: 600.
	CHECK(decoder.set_clips(clips, 0));
	CHECK(decoder.total_ticks() == 2400);
	CHECK(decoder.edit_list().has_speed_changes());
	CHECK(decoder.pts() == 0);

	// Every output frame time, forward: what a playing program asks for.
	for (int64_t t = 0; t < 2400; t += FT) {
		CHECK(decoder.show_at(t));
		int file, frame, want_file, want_frame;
		shown(decoder, &file, &frame);
		expected_frame(decoder.edit_list(), t, &want_file, &want_frame);
		CHECK_MSG(file == want_file && frame == want_frame, "t=%lld: showing file %d frame %d, wanted file %d frame %d", (long long)t, file, frame, want_file, want_frame);
		CHECK_MSG(decoder.pts() <= t, "t=%lld: frame begins at %lld, after it", (long long)t, (long long)decoder.pts());
		CHECK_MSG(t - decoder.pts() < 2 * FT, "t=%lld: frame begins at %lld", (long long)t, (long long)decoder.pts());
	}
	// At half speed each frame stays up for two output frames; at double speed alternate frames are skipped.
	CHECK(decoder.show_at(20 * FT)); int file, frame; shown(decoder, &file, &frame); CHECK(file == 0 && frame == 20);
	CHECK(decoder.show_at(21 * FT)); shown(decoder, &file, &frame); CHECK(frame == 20);
	CHECK(decoder.show_at(22 * FT)); shown(decoder, &file, &frame); CHECK(frame == 21);
	CHECK(decoder.pts() == 22 * FT);
	CHECK(decoder.show_at(60 * FT)); shown(decoder, &file, &frame); CHECK(file == 1 && frame == 10);
	CHECK(decoder.show_at(61 * FT)); shown(decoder, &file, &frame); CHECK(file == 1 && frame == 12);
	CHECK(decoder.show_at(62 * FT)); shown(decoder, &file, &frame); CHECK(file == 1 && frame == 14);
	// Times part way through a frame.
	CHECK(decoder.show_at(21 * FT + 28)); shown(decoder, &file, &frame); CHECK(frame == 20);
	CHECK(decoder.show_at(22 * FT + 1)); shown(decoder, &file, &frame); CHECK(frame == 21);

	// advance_to() in a slowed-down clip lands on the frame the time is in, not the one after.
	CHECK(decoder.advance_to(21 * FT + 5)); shown(decoder, &file, &frame); CHECK(frame == 20 && decoder.pts() == 20 * FT);
	CHECK(decoder.advance_to(20 * FT)); shown(decoder, &file, &frame); CHECK(frame == 20);
	CHECK(decoder.advance_to(22 * FT)); shown(decoder, &file, &frame); CHECK(frame == 21);
	// Where it isn't, it is the next one still.
	CHECK(decoder.advance_to(5 * FT + 5)); shown(decoder, &file, &frame); CHECK(file == 0 && frame == 6);
	CHECK(decoder.advance_to(61 * FT + 5)); shown(decoder, &file, &frame); CHECK(file == 1 && frame == 13);

	// In any order: jumping about, backward as well.
	srand(11);
	for (int i = 0; i < 400; i++) {
		const int64_t t = (int64_t)(rand() % 2400);
		CHECK(decoder.show_at(t));
		int want_file, want_frame;
		shown(decoder, &file, &frame);
		expected_frame(decoder.edit_list(), t, &want_file, &want_frame);
		CHECK_MSG(file == want_file && frame == want_frame, "t=%lld: showing file %d frame %d, wanted file %d frame %d", (long long)t, file, frame, want_file, want_frame);
	}
	// The end of the video shows the last frame.
	CHECK(decoder.show_at(2400));
	shown(decoder, &file, &frame);
	CHECK(file == 1 && frame >= 48);
	CHECK(decoder.show_at(100000));
	shown(decoder, &file, &frame);
	CHECK(file == 1 && frame >= 48);
}

static void test_decoder_reads_footage_in_order() {
	Setup s = make_setup(1, 100);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 10 * FT, 0.25, AUDIO_KEEP_PITCH));     // 10 frames, 4 times as long.
	clips.push_back(Clip(0, 50 * FT, 70 * FT, 4.0, AUDIO_KEEP_PITCH));
	CHECK(decoder.set_clips(clips, 0));
	CHECK(decoder.total_ticks() == 10 * FT * 4 + 20 * FT / 4);
	// read_frame() goes through every frame of the footage, one after another, with the frame's time on the timeline.
	int count = 1;
	int64_t last = decoder.pts();
	CHECK(last == 0);
	int file, frame;
	shown(decoder, &file, &frame);
	CHECK(frame == 0);
	while (decoder.read_frame()) {
		shown(decoder, &file, &frame);
		const int want = count < 10 ? count : 50 + (count - 10);
		CHECK_MSG(frame == want, "frame %d: expected %d got %d", count, want, frame);
		CHECK_MSG(decoder.pts() > last, "time went backward at frame %d", count);
		if (count < 10)
			CHECK(decoder.pts() == (int64_t)count * FT * 4);
		else
			CHECK_MSG(decoder.pts() == 10 * FT * 4 + (int64_t)llround((double)(count - 10) * FT / 4.0), "frame %d at %lld", count, (long long)decoder.pts());
		last = decoder.pts();
		count++;
	}
	CHECK(count == 30);
	// The frame on screen at each frame's own time is that frame: advance_to(pts()) is the identity.
	for (int target_frame = 0; target_frame < 30; target_frame++) {
		const int64_t t = decoder.edit_list().timeline_for(target_frame < 10 ? 0 : 1, (target_frame < 10 ? target_frame : 50 + (target_frame - 10)) * FT);
		CHECK(decoder.advance_to(t));
		shown(decoder, &file, &frame);
		const int want = target_frame < 10 ? target_frame : 50 + (target_frame - 10);
		CHECK_MSG(frame == want, "advance_to(%lld): expected frame %d got %d", (long long)t, want, frame);
		const int64_t again = decoder.pts();
		CHECK(decoder.advance_to(again));
		shown(decoder, &file, &frame);
		CHECK_MSG(frame == want, "advance_to(pts) moved from frame %d to %d", want, frame);
	}
}

static void test_decoder_awkward_speeds() {
	Setup s = make_setup(1, 200);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	double speeds[] = { 0.1, 0.3, 0.77, 1.3, 1.7, 3.0, 7.9, 16.0 };
	for (size_t k = 0; k < sizeof(speeds) / sizeof(speeds[0]); k++) {
		std::vector<Clip> clips;
		clips.push_back(Clip(0, 0, 20 * FT));
		clips.push_back(Clip(0, 20 * FT, 150 * FT, speeds[k], AUDIO_KEEP_PITCH));
		clips.push_back(Clip(0, 150 * FT, 160 * FT));
		CHECK(decoder.set_clips(clips, 0));
		// Every frame, read in order, comes back when its own time is asked for.
		std::vector<int64_t> times;
		times.push_back(decoder.pts());
		while (decoder.read_frame())
			times.push_back(decoder.pts());
		CHECK(times.size() == 160);
		for (size_t i = 0; i < times.size(); i++) {
			CHECK(decoder.advance_to(times[i]));
			int file, frame;
			shown(decoder, &file, &frame);
			CHECK_MSG(frame == (int)i, "speed %g: advance_to(frame %zu's time %lld) landed on frame %d", speeds[k], i, (long long)times[i], frame);
			if (g_failures > 10) return;
		}
		// And show_at() at a scatter of times agrees with the edit.
		for (int i = 0; i < 100; i++) {
			const int64_t t = (int64_t)(rand() % decoder.total_ticks());
			CHECK(decoder.show_at(t));
			int file, frame, want_file, want_frame;
			shown(decoder, &file, &frame);
			expected_frame(decoder.edit_list(), t, &want_file, &want_frame);
			// (Rounding in the conversions can put a time that is just at a frame's start on the one before.)
			CHECK_MSG(frame == want_frame || frame == want_frame - 1, "speed %g t=%lld: frame %d wanted %d", speeds[k], (long long)t, frame, want_frame);
		}
	}
}

static void test_decoder_stepping_and_switching_back() {
	Setup s = make_setup(1, 100);
	MediaDecoder decoder;
	std::string error;
	CHECK(decoder.open(s.paths, &error));
	std::vector<Clip> clips;
	clips.push_back(Clip(0, 0, 30 * FT, 0.5, AUDIO_KEEP_PITCH));
	clips.push_back(Clip(0, 30 * FT, 60 * FT));
	CHECK(decoder.set_clips(clips, 0));
	int file, frame;
	// Stepping goes frame by frame of the footage.
	CHECK(decoder.step_frames(1)); shown(decoder, &file, &frame); CHECK(frame == 1 && decoder.pts() == 2 * FT);
	CHECK(decoder.step_frames(5)); shown(decoder, &file, &frame); CHECK(frame == 6 && decoder.pts() == 12 * FT);
	CHECK(decoder.step_frames(-2)); shown(decoder, &file, &frame); CHECK(frame == 4 && decoder.pts() == 8 * FT);
	// Back to normal speed: the same footage, half as long again, and the picture asked for stays put.
	clips[0].speed = 1.0;
	CHECK(decoder.set_clips(clips, 4 * FT));
	shown(decoder, &file, &frame);
	CHECK(frame == 4 && decoder.pts() == 4 * FT && decoder.total_ticks() == 60 * FT);
	CHECK(!decoder.edit_list().has_speed_changes());
}

int main() {
	test_edit_list_arithmetic();
	test_round_trips_with_awkward_speeds();
	test_model_checks_speeds();
	test_cuts_carry_the_speed();
	test_set_speed_edit();
	test_audio_plan_carries_speed();
	test_decoder_shows_the_right_frames();
	test_decoder_reads_footage_in_order();
	test_decoder_awkward_speeds();
	test_decoder_stepping_and_switching_back();
	if (g_failures == 0)
		printf("speed_test: all ok\n");
	return g_failures == 0 ? 0 : 1;
}
