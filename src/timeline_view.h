#pragma once

// The timeline editor: the clips of the edit timeline laid out left to right as blocks named for
// their files, with a ruler above them, a mark where each join is, and the playhead. The mouse
// moves around it: left-drag (or click) scrubs, the right button drags to scan forward and back,
// the wheel zooms about the cursor, and the middle button, Shift and the wheel, or a left-drag on
// the ruler pans. It only
// moves around, and the buttons along its top cut the clips at the playhead (see timeline_editor.h).
//
// Drawn with ImGui, so it is the window's own, never part of the exported picture. The arithmetic
// of what's showing is in timeline_scale.h.

#include <stdint.h>

#include "edit_model.h"
#include "timeline_scale.h"
#include "window_layout.h"

class MediaContainerMgr;
class TimelineEditor;

class TimelineView {
public:
	TimelineView();

	// Draws the timeline filling `rect` (window pixels) and acts on the mouse in it. Call once a
	// frame between ImGui::NewFrame() and ImGui::Render(). `model` supplies the clips; `media` is
	// where the playhead is, and what is moved when the user moves it. The buttons above the
	// timeline (Split, Trim Left and so on) are carried out by `editor`, on `model` and `media`.
	void draw(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, TimelineEditor& editor);

private:
	TimelineScale m_scale;

	// Left button on the ruler: grabbing the timeline to slide it left and right.
	bool    m_panning;

	// Left button: scrubbing. The fast, approximate seek follows the mouse; letting go lands on the
	// exact frame.
	bool    m_scrubbing;
	int64_t m_scrub_ticks;

	// Right button: scanning at a rate set by how far the mouse is from where it was pressed.
	bool    m_scanning;
	float   m_scan_origin_x;
	float   m_scan_accumulator;

	// Where the playhead was last frame, to bring it back into view when it leaves. While playing,
	// the view follows the playhead; the user sliding or zooming the view by hand turns that off, so
	// the view isn't dragged back under their hand, and it comes back on once the playhead is in
	// view again or the user scrubs or scans.
	int64_t m_last_playhead;
	bool    m_follow;
};
