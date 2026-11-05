#pragma once

// The timeline editor: the clips of the edit timeline laid out left to right as blocks filled with
// small pictures of the video, each with its file's name in the corner and its file's colour round
// it and on the ruler above it, a mark where each join is, and the playhead. The mouse
// moves around it: left-drag (or click) scrubs, the right button drags to scan forward and back,
// the wheel zooms about the cursor, and the middle button, Shift and the wheel, or a left-drag on
// the ruler pans. Ctrl and the left button together pick up a clip and drop it in another place in
// the order. Below the clips, when the project has text boxes (see text_box.h), is the text track: each box a block as long
// as it shows, with the start of its words in it, on rows of their own where boxes overlap. A click on a box selects it;
// Ctrl and the left button on its body move it in time, and on either end of it change when it starts or stops (the box
// stays tied to a frame of the video: see text_lanes.h). The buttons along its top cut the clips at the playhead (see timeline_editor.h); a
// timeline with no editor, such as the one in the sync view, has no such buttons.
//
// Drawn with ImGui, so it is the window's own, never part of the exported picture. The arithmetic
// of what's showing is in timeline_scale.h.

#include <stdint.h>
#include <string>

#include "edit_model.h"
#include "text_box.h"
#include "text_lanes.h"
#include "timeline_scale.h"
#include "window_layout.h"

class MediaContainerMgr;
class ThumbnailStore;
class TimelineEditor;

// What the user asked for by dropping a clip somewhere else: clip `from` is to become clip `to`
// (see timeline_edit::move_clip). Whoever is showing the timeline carries it out, as what it means
// differs: in the editor it rearranges the clips, in the sync view the order of the files.
struct ReorderRequest {
	bool requested;
	int  from;
	int  to;
	ReorderRequest() : requested(false), from(0), to(0) {}
};

class TimelineView {
public:
	// `window_name` is the ImGui window the timeline is drawn in; two timelines that can be on screen
	// at once need different ones.
	explicit TimelineView(const char* window_name = "Timeline");

	// Draws the timeline filling `rect` (window pixels) and acts on the mouse in it. Call once a
	// frame between ImGui::NewFrame() and ImGui::Render(). `model` supplies the clips; `media` is
	// where the playhead is, and what is moved when the user moves it. The buttons above the
	// timeline (Split, Trim Left and so on) are carried out by `editor`, on `model` and `media`; with
	// no editor (null) they are not shown. When the user drops a dragged clip, `reorder` is set.
	// The text track is shown only with an editor, and `selected_text` (the id of the selected text box, or 0 for
	// none) is where it reads which box is selected and tells of a click on another.
	void draw(const LayoutRect& rect, EditModel& model, MediaContainerMgr& media, TimelineEditor* editor, ReorderRequest* reorder,
		int* selected_text = nullptr);

	// Shows the whole timeline.
	void fit() { m_scale.fit(); }

	// When a new row of text boxes has appeared on the text track and the timeline is too short to show it, this is how tall the
	// timeline should be to (0 otherwise). Asking clears it. The caller makes the timeline that tall (the layout holds it to what
	// the window allows).
	float take_height_request() { const float wanted = m_height_request; m_height_request = 0.0f; return wanted; }

	// Where the pictures on the clips come from. Without one (the default) the clips are plain blocks.
	void set_thumbnails(ThumbnailStore* thumbnails) { m_thumbnails = thumbnails; }

private:
	std::string     m_window_name;
	TimelineScale   m_scale;
	ThumbnailStore* m_thumbnails;

	// Left button on the ruler: grabbing the timeline to slide it left and right.
	bool    m_panning;

	// Left button: scrubbing. The fast, approximate seek follows the mouse; letting go lands on the
	// exact frame.
	bool    m_scrubbing;
	int64_t m_scrub_ticks;

	// Ctrl and the left button: dragging a clip (m_drag_clip, -1 when not) to another place in the
	// order, where it would go if dropped now being m_drag_to.
	int     m_drag_clip;
	int     m_drag_to;

	// Ctrl and the left button on a text box: dragging it (m_text_drag_id, 0 when not) by its body or one end (a
	// text_lanes::Part). It began as m_text_drag_original, showing m_text_drag_span, with the mouse at m_text_drag_origin_x.
	int              m_text_drag_id;
	int              m_text_drag_part;
	TextBox          m_text_drag_original;
	text_lanes::Span m_text_drag_span;
	float            m_text_drag_origin_x;

	// The number of rows of text boxes last time, to notice a new one, and the height the timeline was found to need then.
	int     m_previous_lane_count;
	float   m_height_request;

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
