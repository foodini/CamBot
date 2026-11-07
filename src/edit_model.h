#pragma once

// The editing model: what the user has put together, as plain data with no FFmpeg or OpenGL in
// it, so it can be tested on its own.
//
// There are two clocks.
//
//   The flight clock is the capture timeline: every media file of the library, uncut, laid end to
//   end in "capture order" (the order the camera recorded them). Telemetry is synchronised to
//   this clock, because it is about when the footage was recorded, not about how it was edited.
//
//   The edit timeline is what the user builds: a list of clips, each a piece of one media file,
//   played one after the other. Cutting, reordering and reusing footage changes this timeline and
//   never the flight clock.
//
// Every frame on the edit timeline comes from some moment in some file (a FileTime), and that
// moment has a place on the flight clock, which the sync turns into a telemetry time. So a clip
// shows the right telemetry wherever it is dragged to.
//
// All times here are in "ticks": the video time base of the first media file (1/30000 s, say).
// A tick is far shorter than a frame, so one fits everything.

#include <stdint.h>
#include <string>
#include <vector>

#include "file_time.h"
#include "text_box.h"

// What a clip does with its sound when it is not played at normal speed.
enum AudioMode {
	AUDIO_KEEP_PITCH = 0,     // Stretched or squeezed in time, the pitch left alone.
	AUDIO_CHANGE_PITCH = 1,   // Played faster or slower like a tape: the pitch goes up and down with the speed.
	AUDIO_MUTE = 2            // Silent.
};

// How fast a clip can be played: from a tenth of normal speed to sixteen times it.
const double MIN_SPEED = 0.1;
const double MAX_SPEED = 16.0;

// Footage ticks to timeline ticks for a clip played at `speed` (and back), to the nearest tick. At normal speed they are the
// same, exactly.
int64_t footage_to_timeline(int64_t footage_ticks, double speed);
int64_t timeline_to_footage(int64_t timeline_ticks, double speed);

// A piece of one media file: from in_ticks up to (not including) out_ticks, counted from the
// start of the file. It plays at `speed` times normal speed, so it lasts 1/speed as long on the timeline as it does in the
// file: in_ticks and out_ticks are always the file's own time.
struct Clip {
	int     media;
	int64_t in_ticks;
	int64_t out_ticks;
	double  speed;         // MIN_SPEED to MAX_SPEED; 1 is normal.
	int     audio_mode;    // An AudioMode; only matters when the speed isn't 1.
	Clip() : media(0), in_ticks(0), out_ticks(0), speed(1.0), audio_mode(AUDIO_KEEP_PITCH) {}
	Clip(int media_index, int64_t in, int64_t out) : media(media_index), in_ticks(in), out_ticks(out), speed(1.0), audio_mode(AUDIO_KEEP_PITCH) {}
	Clip(int media_index, int64_t in, int64_t out, double clip_speed, int mode)
		: media(media_index), in_ticks(in), out_ticks(out), speed(clip_speed), audio_mode(mode) {}
	// How much of the file the clip uses ("footage"), whatever the speed.
	int64_t duration_ticks() const { return out_ticks - in_ticks; }
	// How long the clip lasts on the timeline.
	int64_t timeline_ticks() const { return footage_to_timeline(out_ticks - in_ticks, speed); }
	bool operator==(const Clip& other) const {
		return media == other.media && in_ticks == other.in_ticks && out_ticks == other.out_ticks && speed == other.speed &&
			audio_mode == other.audio_mode;
	}
	bool operator!=(const Clip& other) const { return !(*this == other); }
};

// How the picture and sound give way at a seam of the edit. The seams are numbered from 0: seam 0 is the start of the video, seam i
// (1 to count-1) the join between clip i-1 and clip i, and seam `count` the end of the video, so a list of n clips has n+1 of them.
// A cut (the usual) is an instant. A fade, at the start or the end of the video only, takes `ticks` of the timeline to go up
// from black (at the start) or down to black (at the end); the picture, whatever is drawn on it and the sound all fade together.
// A crossfade, at a join only, lays the last `ticks` of one clip over the first `ticks` of the next: both clips play at once for that
// long, the picture from one giving way to the other (and the sound likewise), so the timeline is `ticks` shorter than the two clips
// end to end, and the footage of both is used in full (nothing is held or borrowed). No transition may last longer than half of the
// shorter of the clips either side of it, so the two ends of a clip never meet.
enum TransitionType { TRANSITION_CUT = 0, TRANSITION_CROSSFADE = 1, TRANSITION_FADE = 2 };

struct Transition {
	int     type;
	int64_t ticks;       // Timeline ticks; 0 for a cut.
	Transition() : type(TRANSITION_CUT), ticks(0) {}
	Transition(int transition_type, int64_t length) : type(transition_type), ticks(length) {}
	bool is_cut() const { return type == TRANSITION_CUT || ticks <= 0; }
	bool operator==(const Transition& other) const { return type == other.type && ticks == other.ticks; }
	bool operator!=(const Transition& other) const { return !(*this == other); }
};

// The edit timeline: clips played back to back. Clip i starts where clip i-1 ends.
//
// A clip that isn't played at normal speed lasts more or less time on the timeline than the footage it uses. So there are two
// ways to count along the edit: timeline ticks, which is what the user sees and everything else here uses, and footage ticks,
// which are what you get if every clip is laid down at normal speed (the video decoder works in those, as it reads each file
// straight through). The two are the same for an edit with no speed changes.
class EditList {
public:
	EditList();

	void clear();
	// Sets the clips, with every transition a cut.
	void set(const std::vector<Clip>& clips);
	// Sets the clips and the transitions between them (count + 1 of them, see Transition; any other number, and they are all cuts).
	// Nothing here checks that they are allowed (see transitions_valid()).
	void set(const std::vector<Clip>& clips, const std::vector<Transition>& transitions);

	// ---- Transitions ----
	// The transition at seam `seam` (a cut if there is no such seam).
	Transition               transition(int seam) const { return seam >= 0 && seam < (int)m_transitions.size() ? m_transitions[seam] : Transition(); }
	const std::vector<Transition>& transitions() const { return m_transitions; }
	bool                     has_transitions() const;
	// The longest a transition at `seam` may be, in timeline ticks: half the shorter of the clips either side (0 if there is no such seam).
	int64_t                  max_transition_ticks(int seam) const;
	// Whether every transition is of a kind its seam allows and no longer than it may be.
	bool                     transitions_valid() const;
	// How much of the picture (and sound) shows at `timeline_ticks`, 0 to 1: less than 1 while a fade at the start or the end is
	// in progress.
	double                   fade_level(int64_t timeline_ticks) const;

	int                      count() const { return (int)m_clips.size(); }
	const Clip&              clip(int index) const { return m_clips[index]; }
	const std::vector<Clip>& clips() const { return m_clips; }
	// Where clip `index` begins on the timeline. A clip with a crossfade into it begins that much before the clip before it ends.
	int64_t                  start(int index) const { return m_starts[index]; }
	// Where clip `index` ends on the timeline. (Inside the start of the clip after it, if there is a crossfade between them.)
	int64_t                  end(int index) const { return m_starts[index] + m_clips[index].timeline_ticks(); }
	int64_t                  total_ticks() const { return m_total; }
	// How long the two clips either side of seam `seam` overlap: the length of the crossfade there, 0 for anything else.
	int64_t                  overlap_ticks(int seam) const { return seam >= 1 && seam < (int)m_overlaps.size() ? m_overlaps[seam] : 0; }
	// Whether any two clips overlap (so that the picture is sometimes made from two of them).
	bool                     has_crossfades() const { return m_crossfades; }

	// The clip a timeline time falls in. Times before the start give the first clip and times at
	// or past the end give the last. -1 if there are no clips. Where two clips overlap (a crossfade) it is the one that shows more
	// of itself: the earlier clip for the first half of the overlap, the later for the second. This is the clip that the sound,
	// the overlays and the edits take the time to belong to.
	int                      clip_index_for(int64_t timeline_ticks) const;

	// What shows at a timeline time: clip `first` alone (second is -1) or, in a crossfade, clip `first` fading into clip `second = first + 1`,
	// `weight` (0 to 1) of the way, a straight line from the first to the second.
	struct Mix {
		int    first;
		int    second;
		double weight;
		Mix() : first(-1), second(-1), weight(0.0) {}
	};
	Mix                      mix_at(int64_t timeline_ticks) const;
	// The footage time that clip `index` has at `timeline_ticks` (which may be anywhere in the clip, overlap included), kept inside the clip.
	int64_t                  footage_for_clip_at(int index, int64_t timeline_ticks) const;
	// Where in the media a timeline time comes from.
	FileTime                 source_for(int64_t timeline_ticks) const;
	// The timeline time of a moment in clip `index`'s media, kept inside that clip.
	int64_t                  timeline_for(int index, int64_t local_ticks) const;

	// ---- Footage ticks (every clip at normal speed) ----
	bool                     has_speed_changes() const { return m_speed_changes; }
	int64_t                  footage_start(int index) const { return m_footage_starts[index]; }
	int64_t                  footage_total() const { return m_footage_total; }
	int                      clip_index_for_footage(int64_t footage_ticks) const;
	FileTime                 source_for_footage(int64_t footage_ticks) const;
	// The footage time of clip `index`'s moment `local_ticks` of its media, kept inside the clip.
	int64_t                  footage_for(int index, int64_t local_ticks) const;
	// Timeline time to footage time and back (both kept inside the edit).
	int64_t                  footage_for_timeline(int64_t timeline_ticks) const;
	int64_t                  timeline_for_footage(int64_t footage_ticks) const;

private:
	std::vector<Clip>    m_clips;
	std::vector<Transition> m_transitions;   // count + 1 of them, or none when there are no clips.
	std::vector<int64_t> m_starts;
	std::vector<int64_t> m_overlaps;         // Per seam (count + 1 of them): how far the clip after it starts before the clip before it ends.
	std::vector<int64_t> m_switches;         // Per clip: the time from which it shows more of itself than the clip before.
	int64_t              m_total;
	std::vector<int64_t> m_footage_starts;
	int64_t              m_footage_total;
	bool                 m_speed_changes;
	bool                 m_crossfades;
};

// The flight clock: the media files in the order they were recorded, end to end.
class CaptureSequence {
public:
	CaptureSequence();

	// `order` lists media indices in capture order and must use each of 0..count-1 once;
	// `durations` has one length per media file. False, changing nothing, if not.
	bool set(const std::vector<int>& order, const std::vector<int64_t>& durations);
	static bool is_valid_order(const std::vector<int>& order, int media_count);

	const std::vector<int>& order() const { return m_order; }
	// Where media file `media` begins on the flight clock.
	int64_t flight_start(int media) const { return m_starts[media]; }
	int64_t total_ticks() const { return m_total; }
	int64_t flight_ticks(const FileTime& time) const { return m_starts[time.media] + time.local_ticks; }
	// The file and moment at a flight-clock time (clamped to the sequence).
	FileTime file_time_for(int64_t flight_ticks) const;

private:
	std::vector<int>     m_order;
	std::vector<int64_t> m_starts;     // Indexed by media, not by position in the order.
	std::vector<int64_t> m_durations;
	int64_t              m_total;
};

// One observation that ties the footage to the telemetry: "this moment of this file is this
// time on the telemetry's clock". Launch and landing are the two main ones. Because it names a
// file, it stays true however the files are ordered.
struct SyncAnchor {
	FileTime at;
	float    telemetry_seconds;
	SyncAnchor() : telemetry_seconds(0.0f) {}
	SyncAnchor(const FileTime& file_time, float seconds) : at(file_time), telemetry_seconds(seconds) {}
	bool operator==(const SyncAnchor& other) const { return at == other.at && telemetry_seconds == other.telemetry_seconds; }
	bool operator!=(const SyncAnchor& other) const { return !(*this == other); }
};

class EditModel {
public:
	EditModel();

	// ---- The library ----
	// Sets the media files and their lengths, and starts over: capture order is the order given,
	// and the edit timeline is every file whole. Anchors and the launch mark are cleared.
	void set_media(const std::vector<std::string>& paths, const std::vector<int64_t>& durations, double seconds_per_tick);
	int                 media_count() const { return (int)m_paths.size(); }
	const std::string&  media_path(int media) const { return m_paths[media]; }
	int64_t             media_duration(int media) const { return m_durations[media]; }
	double              seconds_per_tick() const { return m_seconds_per_tick; }
	double              ticks_to_seconds(int64_t ticks) const { return (double)ticks * m_seconds_per_tick; }
	int64_t             seconds_to_ticks(double seconds) const;

	// ---- Capture order (the flight clock) ----
	bool                    set_capture_order(const std::vector<int>& order);
	const CaptureSequence&  capture() const { return m_capture; }

	// ---- The edit timeline ----
	// False, changing nothing, if any clip names a missing media file or doesn't lie inside it.
	bool                    set_clips(const std::vector<Clip>& clips);
	// The same with the transitions between them (see EditList::set()). False, changing nothing, if the clips are no good or the
	// transitions are not valid for them.
	bool                    set_edit(const std::vector<Clip>& clips, const std::vector<Transition>& transitions);
	void                    set_default_clips();    // Every file whole, in capture order.
	const EditList&         edit_list() const { return m_edit_list; }
	// Whether the join at seam `seam` (1 to count - 1) is between footage that carries on without a break: the same file going straight
	// on (a clip that has been split), or the end of one file and the start of the next in capture order (what a camera that cuts its
	// recording into files makes). Nothing is lost at such a join, so a transition there is not wanted.
	bool                    join_is_continuous(int seam) const;
	// True once the user has changed the edit timeline by hand. From then on changes that move
	// telemetry under clips already made are warned about (see clips_with_changed_telemetry).
	bool                    edit_started() const { return m_edit_started; }
	void                    set_edit_started(bool started) { m_edit_started = started; }

	// ---- The source view ----
	// While the user is lining up the telemetry and ordering the files, the video shows every file
	// whole, in capture order, whatever the edit is. begin_source_view() puts that on the edit timeline
	// and keeps the user's real clips aside; end_source_view() brings them back. The project is saved
	// with the real clips either way (see clips_to_keep), so a save, an autosave or a quit in the
	// middle of it can't lose the edit.
	bool                    in_source_view() const { return m_in_source_view; }
	void                    begin_source_view();
	void                    end_source_view();
	// Changing the capture order shows in the source view: the files are laid out again in the new order.
	void                    refresh_source_view() { if (m_in_source_view) set_default_clips(); }
	const std::vector<Clip>& clips_to_keep() const { return m_in_source_view ? m_stashed_clips : m_edit_list.clips(); }
	const std::vector<Transition>& transitions_to_keep() const { return m_in_source_view ? m_stashed_transitions : m_edit_list.transitions(); }
	bool                    edit_started_to_keep() const { return m_in_source_view ? m_stashed_edit_started : m_edit_started; }

	// ---- Telemetry sync ----
	// Telemetry time = flight time - offset, where the offset is worked out from the anchors: it
	// is the offset at the earliest anchor before that, at the latest after it, and in between
	// moves in a straight line from one anchor's to the next (so a camera clock that runs a bit
	// fast or slow is spread evenly instead of showing as a jump). With one anchor it is a plain
	// constant, and with none the two clocks are taken to agree.
	const std::vector<SyncAnchor>& anchors() const { return m_anchors; }
	void                    set_anchors(const std::vector<SyncAnchor>& anchors);
	double                  telemetry_seconds(double flight_seconds) const;
	double                  offset_at(double flight_seconds) const;
	// The offset at the earliest anchor (0 with none); the plain-offset view of the sync, for the
	// single-offset controls.
	double                  offset() const;
	// Makes the offset `offset`: creates the one anchor at the start of the flight if there is
	// none, otherwise moves every anchor together.
	void                    set_offset(double offset);
	void                    add_to_offset(double delta);

	// ---- Text boxes ----
	// The text laid over the video (see text_box.h). Each belongs to a frame of the footage, so it goes where
	// its footage goes; how long it shows is its own.
	const std::vector<TextBox>& text_boxes() const { return m_text_boxes; }
	// Replaces all of them (as when reading a project or undoing).
	void                    set_text_boxes(const std::vector<TextBox>& boxes);
	// Adds a box, giving it an id no other has; returns the id.
	int                     add_text_box(const TextBox& box);
	bool                    remove_text_box(int id);
	// The box with this id, or null.
	TextBox*                find_text_box(int id);
	const TextBox*          find_text_box(int id) const;
	// Where on the edit timeline the box shows: from its first frame to one past its last. The first frame is
	// where its anchor is if the edit uses that frame, or the nearest footage the edit does use if not.
	void                    text_box_span(const TextBox& box, int64_t* start_ticks, int64_t* end_ticks) const;

	// ---- Launch ----
	const FileTime&         launch_mark() const { return m_launch_mark; }
	// Sets where launch is without saying the user chose it (see launch_marked()): the start of the flight stands in
	// for it until they do.
	void                    set_launch_mark(const FileTime& mark) { m_launch_mark = mark; }
	// Whether the user has marked launch themselves. Until they have, the flight clock and the distance flown count
	// from the start of the footage, which is rarely where the glider left the ground.
	bool                    launch_marked() const { return m_launch_marked; }
	void                    set_launch_marked(bool marked) { m_launch_marked = marked; }
	double                  launch_flight_seconds() const;
	// Marks launch (so launch_marked() is true afterward).
	void                    set_launch_flight_seconds(double seconds);

	// ---- Mapping between the clocks ----
	double                  flight_seconds(const FileTime& time) const;
	FileTime                file_time_for_flight_seconds(double flight_seconds) const;
	// The place on the edit timeline of a moment in a file: where that moment is if the edit uses it, otherwise the
	// closest footage it does use, going by the flight clock (0 if the edit is empty).
	int64_t                 timeline_ticks_nearest(const FileTime& moment) const;
	// The flight time of a timeline time, which is what looks up telemetry.
	double                  flight_seconds_for_timeline(int64_t timeline_ticks) const;
	double                  telemetry_seconds_for_timeline(int64_t timeline_ticks) const;
	// The stretch of the flight clock the clips use, whatever order they are in: from the earliest
	// moment any clip starts at to the latest moment any clip ends at (so it spans any footage cut out
	// of the middle too). False, setting nothing, if there are no clips.
	bool                    used_flight_span(double* start_seconds, double* end_seconds) const;
	// The same stretch in telemetry time, through the sync. This is the telemetry worth showing: what
	// was recorded before the first frame used, or after the last, is not.
	bool                    used_telemetry_span(double* start_seconds, double* end_seconds) const;

	// How many of the clips in `before` would show different telemetry under `after` (the same
	// media and clips, after a change to the capture order or the anchors). Used to warn the
	// user, once, before applying a change that reaches into footage they have already edited.
	static int              clips_with_changed_telemetry(const EditModel& before, const EditModel& after);

private:
	void rebuild_sync();

	std::vector<std::string> m_paths;
	std::vector<int64_t>     m_durations;
	double                   m_seconds_per_tick;
	CaptureSequence          m_capture;
	EditList                 m_edit_list;
	bool                     m_edit_started;
	bool                     m_in_source_view;
	std::vector<Clip>        m_stashed_clips;
	std::vector<Transition>  m_stashed_transitions;
	bool                     m_stashed_edit_started;
	std::vector<SyncAnchor>  m_anchors;
	FileTime                 m_launch_mark;
	bool                     m_launch_marked;
	std::vector<TextBox>     m_text_boxes;
	int                      m_next_text_id;

	// The anchors on the flight clock, earliest first, for fast lookup.
	struct ResolvedAnchor { double flight_seconds; double offset; };
	std::vector<ResolvedAnchor> m_resolved;
};
