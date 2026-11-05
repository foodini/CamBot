#pragma once

// The Text section of the left panel: the list of the text boxes laid over the video, a button to add one at the
// frame being shown, and, for the one selected, a field to type its words in and a button to delete it. The selected
// box is also drawn on the preview as a dashed rectangle with a handle at each corner: drag a corner to reshape the
// box, or drag inside it to move it. (What else there is to set is in text_box.h.)
//
// Everything it changes goes through the TimelineEditor, so each step can be undone.

#include <string>
#include <vector>

#include "edit_model.h"
#include "text_box_geometry.h"
#include "window_layout.h"

class MediaContainerMgr;
class TimelineEditor;

class TextPanel {
public:
	TextPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor);

	// Draws the section, as part of the window the caller has open. `can_change` is false while an export runs.
	void draw(bool can_change);

	// Draws the selected box on the preview (the rectangle of the window the picture fills) and lets the mouse reshape
	// it. Call it every frame, with the other windows. `can_change` is false while an export runs.
	void draw_on_preview(const LayoutRect& preview, bool can_change);

	// The text box selected in the list, or 0 if none (or if it has since been removed). The Delete key
	// removes this one in place of the clip, if there is one.
	int selected_id() const;
	// Selects a text box (0 for none), as when one is clicked on the timeline.
	void select(int id) { m_selected = id; }

private:
	EditModel*        m_model;
	MediaContainerMgr* m_media;
	TimelineEditor*   m_editor;
	int               m_selected;
	bool              m_field_active;     // The text field has the keyboard.
	int               m_buffer_id;        // Whose words m_buffer holds.
	std::vector<char> m_buffer;           // What the field edits.

	// A drag on the preview: which part of the box is being dragged (-1 for none, otherwise a text_box_geometry::Corner
	// or Edge, or BODY), and the box and the mouse as they were when it began, in fractions of the frame.
	static const int BODY = 8;
	int               m_drag_part;
	text_box_geometry::Rect m_drag_start_box;
	float             m_drag_start_mouse_x;
	float             m_drag_start_mouse_y;
};
