#pragma once

// The sync view: a mode that takes over the window while the user lines the telemetry up with the
// video and puts the video files in order (see window_layout.h for where everything goes). While it
// is on, the video plays every file whole, in capture order, whatever the edit is; the user's edit
// is kept aside by the EditModel (see EditModel::begin_source_view) and comes back when the user
// finishes. "Done" keeps what was changed in here (the sync and the order), "Cancel" puts them back
// as they were when the view was opened.
//
// This class owns the mode's bookkeeping and draws its own parts with ImGui: the bar with the Done
// and Cancel buttons, the left pane (the map will be here; for now, where in the footage and the
// telemetry the frame on screen is), and the timeline of the files. That is the same timeline the
// editor uses, showing whole files: click or drag in it to move through the footage, and Ctrl+drag a
// file to put it in another place in the order the footage was recorded. The video itself, the
// transport bar and the divider above the timeline are the main window's, drawn into the rectangles
// the layout gives them.

#include <stdint.h>
#include <vector>

#include "edit_model.h"
#include "timeline_view.h"
#include "window_layout.h"

class MediaContainerMgr;

class SyncView {
public:
	SyncView();

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

	enum Outcome { STAY, DONE, CANCEL };
	// Draws the view's own parts (not the video or the transport bar) and acts on the mouse in them.
	// Call once a frame, between ImGui::NewFrame() and ImGui::Render(), only while active().
	// `has_telemetry` says whether the project has a telemetry file.
	Outcome draw(const SyncLayout& layout, EditModel& model, MediaContainerMgr& media, bool has_telemetry);

private:
	void draw_header(const LayoutRect& rect, Outcome* outcome);
	void draw_map_pane(const LayoutRect& rect, const EditModel& model, const MediaContainerMgr& media, bool has_telemetry);
	// Puts file `from` at position `to` in the order they were recorded; the picture on screen stays.
	void reorder_files(int from, int to, EditModel* model, MediaContainerMgr* media);

	bool    m_active;

	TimelineView m_timeline;

	// How things were when the view was opened.
	int64_t                 m_entry_playhead;      // On the edit timeline.
	std::vector<int>        m_entry_order;
	std::vector<SyncAnchor> m_entry_anchors;
	FileTime                m_entry_launch_mark;
	EditModel               m_entry_model;         // All of it, to see afterward what the changes did to the edit.
};
