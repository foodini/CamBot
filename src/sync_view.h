#pragma once

// The sync view: a mode that takes over the window while the user lines the telemetry up with the
// video and puts the video files in order (see window_layout.h for where everything goes). While it
// is on, the video plays every file whole, in capture order, whatever the edit is; the user's edit
// is kept aside by the EditModel (see EditModel::begin_source_view) and comes back when the user
// finishes. "Done" keeps what was changed in here (the sync and the order), "Cancel" puts them back
// as they were when the view was opened.
//
// This class owns the mode's bookkeeping and draws its own parts with ImGui: the bar with the Done
// and Cancel buttons, the map on the left (the whole flight's path, with the place and heading of the
// frame on screen; wheel to zoom, drag to move it, and it follows the frame on screen unless the user
// has moved it), the strip of buttons between the map and the video, and the timeline of the files.
// That is the same timeline the editor uses, showing whole files: click or drag in it to move through
// the footage, and Ctrl+drag a file to put it in another place in the order the footage was recorded. The video itself, the
// transport bar and the divider above the timeline are the main window's, drawn into the rectangles
// the layout gives them.

#include <stdint.h>
#include <vector>

#include "edit_model.h"
#include "map_tiles.h"
#include "map_view.h"
#include "tile_layer.h"
#include "timeline_view.h"
#include "window_layout.h"

class MediaContainerMgr;
struct ImDrawList;

class SyncView {
public:
	// `tiles` is the program's map picture layer (see tile_layer.h), shared with whatever fetches ahead of time.
	explicit SyncView(TileLayer* tiles);

	bool active() const { return m_active; }

	// Opens the view. The frame on screen stays on screen. False, changing nothing, if the video
	// could not be set up to play the files whole.
	bool enter(EditModel* model, MediaContainerMgr* media);
	// Whether it is all right to close the view keeping what was changed: true unless the file order
	// (or, later, the sync) has changed what the telemetry shows under clips the user has already
	// cut, in which case the user is asked, and true only if they say to go ahead.
	bool confirm_done(const EditModel& model) const;
	// Closes the view and brings the edit back, showing the frame that was on screen when it was
	// opened. `keep_changes` false also puts the sync, the launch mark and the file order back as they were.
	void leave(bool keep_changes, EditModel* model, MediaContainerMgr* media);

	// Undo and redo of what has been changed in here: the sync (by clicking or sliding on the map, or by the
	// nudge keys), the start of the activity, and the order of the files. They don't move the video (so the
	// user can stay on one frame and flip between two syncs). Each returns whether anything was done.
	bool undo(EditModel* model, MediaContainerMgr* media);
	bool redo(EditModel* model, MediaContainerMgr* media);

	enum Outcome { STAY, DONE, CANCEL };
	// Draws the view's own parts (not the video or the transport bar) and acts on the mouse in them.
	// Call once a frame, between ImGui::NewFrame() and ImGui::Render(), only while active().
	// `has_telemetry` says whether the project has a telemetry file.
	Outcome draw(const SyncLayout& layout, EditModel& model, MediaContainerMgr& media, bool has_telemetry);

private:
	void draw_header(const LayoutRect& rect, Outcome* outcome);
	void draw_map_pane(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, bool has_telemetry);
	void draw_map_contents(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, bool has_telemetry);
	// The map's picture: brings the tiles up to date for what the pane shows and draws them, darkened.
	// If the street map can't be had at all, switches to the satellite pictures and says so.
	void draw_tiles(ImDrawList* draw, const LayoutRect& rect);
	// The strip between the map and the video: the buttons that apply to both.
	void draw_strip(const LayoutRect& rect, EditModel& model, const MediaContainerMgr& media);
	// Adds the samples the telemetry has read since the last time to the trace the map draws.
	void update_trace();
	// Puts file `from` at position `to` in the order they were recorded; the picture on screen stays.
	void reorder_files(int from, int to, EditModel* model, MediaContainerMgr* media);

	// What undo and redo keep: everything that can be changed in here.
	struct SyncState {
		std::vector<int>        order;
		std::vector<SyncAnchor> anchors;
		FileTime                launch_mark;
		bool operator==(const SyncState& other) const {
			return order == other.order && anchors == other.anchors && launch_mark == other.launch_mark;
		}
		bool operator!=(const SyncState& other) const { return !(*this == other); }
	};
	static SyncState snapshot(const EditModel& model);
	// Notes that the model may have changed since the last time it was looked at, and if so keeps what it was
	// before so undo can bring it back. `nudge` says it is the kind of change that comes in a quick run (the
	// nudge keys, held): those, one close after another, are one step. A slide (m_slide_group) is one step too.
	void record_change(const EditModel& model, bool nudge);
	void apply_state(const SyncState& state, EditModel* model, MediaContainerMgr* media);

	// Sets the sync so the frame on screen is at the point `sample` (a number of telemetry samples, between
	// them too) of the path, and pauses.
	void apply_pick(double sample, EditModel* model, MediaContainerMgr* media);
	// Which point of the path a click at (x, y) on the map would pick: the passes of the path under it, in the
	// order of how close they are in time to where the frame on screen is now (a repeated click at the same
	// spot goes on to the next in that same order, round and round). `index` is which of `count` it is.
	// `reference` is what the passes were put in order by (see m_cycle_reference).
	bool pick_candidate(float x, float y, double current_sample, double* sample, int* index, int* count, double* reference) const;

	bool    m_active;

	TimelineView m_timeline;

	// The picture under the map.
	TileLayer*                m_tiles;
	std::vector<TileQuad>     m_quads;          // What the tiles layer said to draw on the last frame.
	int                       m_style;          // map_tiles::Style: street map or satellite.
	bool                      m_style_fallback; // The street map wasn't answering, so the satellite pictures were put up instead.
	double                    m_refresh_note_until;   // ImGui::GetTime() until which to say what Refresh Map did...
	int                       m_refresh_note_count;   // ... how many tiles it asked about.

	// The map: the path of the whole flight (every sample of the telemetry file, not only what the edit
	// uses), and what part of the world the pane shows.
	MapTrace    m_trace;
	MapViewport m_map;
	bool        m_follow;         // The view keeps the frame on screen in sight; dragging the map or fitting it turns it off, Center on.
	double      m_last_sample;    // Where the frame on screen was in the telemetry on the last frame, to tell when it moves.
	float       m_ahead_x;        // Which way the flight has been heading lately, as a unit vector shrunk by turning
	float       m_ahead_y;        // (a steady course is length 1, a turn back on itself cancels out to 0); the map gives that side room.
	uint32_t    m_trace_position_version;   // TelemetryMgr's versions the trace was built from; it is built again when they change.
	uint32_t    m_trace_climb_version;
	bool        m_fit_pending;    // Show the whole path, until the user moves the map or the telemetry is all read.
	bool        m_map_pressed;    // The left button went down on the map and is still down.
	bool        m_map_dragging;   // ... and has moved far enough that it is moving the map, not clicking on it.
	bool        m_scanning;       // The right button went down on the map and is still down: the further from where it went down, the faster the sync moves.
	float       m_scan_origin_x;
	float       m_scan_accumulator;   // Samples still owed to the sync (a fraction of one).
	float       m_press_x;
	float       m_press_y;
	float       m_drag_x;         // Where the mouse was on the last frame of a drag.
	float       m_drag_y;
	enum Gesture { GESTURE_PAN, GESTURE_SLIDE, GESTURE_NONE };
	Gesture     m_gesture;        // What the press that is down on the map is doing: moving the map, sliding the sync, or nothing.
	double      m_slide_sample;   // Where on the path the sync was put by the last frame of a slide.

	// Clicking at the same spot again goes to the next pass of the path under it.
	bool        m_cycle_valid;
	float       m_cycle_x;
	float       m_cycle_y;
	double      m_cycle_reference;   // Where the frame on screen was in the telemetry when the clicking at this spot began.
	int         m_cycle_index;       // Which pass the last click picked.
	int         m_cycle_count;       // Out of how many.

	// Undo and redo.
	std::vector<SyncState> m_undo;
	std::vector<SyncState> m_redo;
	SyncState   m_known;             // How things were as of the last look.
	double      m_change_time;       // When (ImGui::GetTime()) they last changed.
	bool        m_change_was_nudge;
	bool        m_slide_group;       // A slide is going on: its changes are all one step...
	bool        m_slide_group_started;   // ... and the step has been kept.

	// How things were when the view was opened.
	int64_t                 m_entry_playhead;      // On the edit timeline.
	std::vector<int>        m_entry_order;
	std::vector<SyncAnchor> m_entry_anchors;
	FileTime                m_entry_launch_mark;
	EditModel               m_entry_model;         // All of it, to see afterward what the changes did to the edit.
};
