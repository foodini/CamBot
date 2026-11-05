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

// A moment in one media file, counted from the start of that file.
struct FileTime {
	int     media;
	int64_t local_ticks;
	FileTime() : media(0), local_ticks(0) {}
	FileTime(int media_index, int64_t ticks) : media(media_index), local_ticks(ticks) {}
	bool operator==(const FileTime& other) const { return media == other.media && local_ticks == other.local_ticks; }
	bool operator!=(const FileTime& other) const { return !(*this == other); }
};

// A piece of one media file: from in_ticks up to (not including) out_ticks, counted from the
// start of the file.
struct Clip {
	int     media;
	int64_t in_ticks;
	int64_t out_ticks;
	Clip() : media(0), in_ticks(0), out_ticks(0) {}
	Clip(int media_index, int64_t in, int64_t out) : media(media_index), in_ticks(in), out_ticks(out) {}
	int64_t duration_ticks() const { return out_ticks - in_ticks; }
	bool operator==(const Clip& other) const {
		return media == other.media && in_ticks == other.in_ticks && out_ticks == other.out_ticks;
	}
	bool operator!=(const Clip& other) const { return !(*this == other); }
};

// The edit timeline: clips played back to back. Clip i starts where clip i-1 ends.
class EditList {
public:
	EditList();

	void clear();
	void set(const std::vector<Clip>& clips);

	int                      count() const { return (int)m_clips.size(); }
	const Clip&              clip(int index) const { return m_clips[index]; }
	const std::vector<Clip>& clips() const { return m_clips; }
	// Where clip `index` begins on the timeline.
	int64_t                  start(int index) const { return m_starts[index]; }
	int64_t                  total_ticks() const { return m_total; }

	// The clip a timeline time falls in. Times before the start give the first clip and times at
	// or past the end give the last. -1 if there are no clips.
	int                      clip_index_for(int64_t timeline_ticks) const;
	// Where in the media a timeline time comes from.
	FileTime                 source_for(int64_t timeline_ticks) const;
	// The timeline time of a moment in clip `index`'s media, kept inside that clip.
	int64_t                  timeline_for(int index, int64_t local_ticks) const;

private:
	std::vector<Clip>    m_clips;
	std::vector<int64_t> m_starts;
	int64_t              m_total;
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
	void                    set_default_clips();    // Every file whole, in capture order.
	const EditList&         edit_list() const { return m_edit_list; }
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

	// ---- Launch ----
	const FileTime&         launch_mark() const { return m_launch_mark; }
	void                    set_launch_mark(const FileTime& mark) { m_launch_mark = mark; }
	double                  launch_flight_seconds() const;
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
	bool                     m_stashed_edit_started;
	std::vector<SyncAnchor>  m_anchors;
	FileTime                 m_launch_mark;

	// The anchors on the flight clock, earliest first, for fast lookup.
	struct ResolvedAnchor { double flight_seconds; double offset; };
	std::vector<ResolvedAnchor> m_resolved;
};
