#pragma once

// The Paths section of the left panel, and the editing of paths on the preview (see tracked_path.h and tracking_path.h for what a
// path is). The section lists the paths, has a button to add one, and, for the one selected, the controls for its name, whether it
// follows the camera's orientation, the circle round the target, and the lens the picture was taken with. The selected path's curve
// is drawn over the picture (Tab hides it).
//
// While a path is being edited, the mouse on the picture is the path's, not the video's: a click puts the path's point for the frame
// shown where it landed (or moves it, there being one point to a frame), and a drag moves it. A click on the point of another frame
// goes to that frame; a drag of it moves it, staying where the video is. Delete takes out the selected point, and the keys that trim
// the clip trim the path to the frame shown (see tracking_path.h). Escape, or Done Editing, gives the mouse back to the video.
//
// Everything it changes goes through the TimelineEditor, so each step can be undone. Nothing here is drawn into the picture, so none of
// it shows in an export or a saved frame.

#include <stdint.h>
#include <string>

#include "edit_model.h"
#include "orientation_loader.h"
#include "tracking_path.h"
#include "window_layout.h"

class MediaContainerMgr;
class TextPanel;
class TimelineEditor;

class PathPanel {
public:
	PathPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor, TextPanel* text_panel);

	// Draws the section, as part of the window the caller has open. `can_change` is false while an export runs.
	void draw(bool can_change);

	// Draws the selected path on the preview (the rectangle of the window the picture fills) and, while editing, takes the mouse there.
	// Call it every frame, with the other windows. `can_change` is false while an export runs.
	void draw_on_preview(const LayoutRect& preview, bool can_change);

	// Whether the mouse on the picture belongs to the path (so a click there must not pause the video).
	bool editing() const { return m_editing; }
	// The keys that mean something else when a path is being edited: they return true if they were taken for the path (and so are not
	// for the clip). Delete takes out the selected point; trimming makes the path begin or end at the frame shown.
	bool delete_pressed();
	bool trim_pressed(bool left);
	// A line about the point under the mouse (which frame it is of and how far from the one shown), for the transport bar; "" for none.
	const std::string& hover_text() const { return m_hover_text; }

private:
	EditModel*         m_model;
	MediaContainerMgr* m_media;
	TimelineEditor*    m_editor;
	TextPanel*         m_text_panel;
	OrientationLoader  m_loader;

	int                m_selected;          // The selected path's id, 0 for none.
	bool               m_editing;
	bool               m_curve_shown;       // Tab turns it off and on.
	bool               m_has_point;         // A point of the selected path is selected, at m_point_ticks.
	int64_t            m_point_ticks;
	std::string        m_hover_text;

	// A press of the mouse on the picture, while editing: it either takes hold of a point (and becomes a drag when the mouse has gone
	// far enough, or a click if it hasn't) or puts the frame's point down and drags it.
	bool               m_pressing;
	bool               m_press_placed;      // The press put the point down (and so is a drag from the start).
	bool               m_press_moved;       // The mouse has gone far enough for it to be a drag.
	int64_t            m_press_ticks;       // The frame of the point being worked on.
	float              m_press_x;           // Where the mouse went down, in window pixels.
	float              m_press_y;
	bool               m_changing;          // A change to the selected path is in progress (an undo step is open; see settle()).

	TrackedPath*       selected_path();
	const TrackedPath* selected_path() const;
	tracking::CameraLens lens() const;
	// What the path's curve is drawn through: the orientations of its file (none until they have been read).
	const tracking::CameraOrientations& orientations(const TrackedPath& path) const;
	void ask_for_orientations(const TrackedPath& path);
	// Closes the undo step of a control that changes the path once the mouse is up.
	void settle();
	void begin_change();
	void stop_editing();
	void start_editing();
	// The timeline time at which the footage frame `local_ticks` of `media` is shown (the nearest to the playhead if it is used more than once);
	// false if the edit does not use it.
	bool timeline_ticks_for(int media, int64_t local_ticks, int64_t* timeline_ticks) const;
	void go_to_frame(int media, int64_t local_ticks);
	void draw_overlay(const LayoutRect& preview, const TrackedPath& path);
	void handle_mouse(const LayoutRect& preview, const TrackedPath& path);
};
