#pragma once

// The Text section of the left panel: the list of the text boxes laid over the video, a button to add one at the
// frame being shown, and, for the one selected, a field to type its words in, a button to delete it, and the controls for
// when it shows (fade in, dwell, fade out) and how it looks (font, size, bold, italic, strikethrough, alignment, colour, and
// a drop shadow). What the user last set is what the next new box starts out as, and is kept for the next run (see
// TextSettings in user_settings.h). The selected
// box is also drawn on the preview as a dashed rectangle with a handle at each corner: drag a corner to reshape the
// box, or drag inside it to move it. (What else there is to set is in text_box.h.)
//
// Everything it changes goes through the TimelineEditor, so each step can be undone.

#include <string>
#include <vector>

#include "edit_model.h"
#include "text_box.h"
#include "text_box_geometry.h"
#include "user_settings.h"
#include "window_layout.h"

class EnvConfig;
class MediaContainerMgr;
class TimelineEditor;

class TextPanel {
public:
	TextPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor, EnvConfig* env_config);

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
	EnvConfig*        m_env_config;
	int               m_selected;
	bool              m_field_active;     // The text field has the keyboard.
	int               m_buffer_id;        // Whose words m_buffer holds.
	std::vector<char> m_buffer;           // What the field edits.

	// What a new box starts out as: how the box last changed looked (see follow_changes()).
	TextSettings      m_defaults;
	bool              m_defaults_unsaved;
	int               m_seen_id;          // The selected box as it was last time, to see when it changes.
	TextBox           m_seen_box;
	// The colour picker open (0 for none, otherwise which colour: PICK_TEXT or PICK_SHADOW), and whether it has changed the colour.
	static const int  PICK_TEXT = 1;
	static const int  PICK_SHADOW = 2;
	int               m_picker;
	bool              m_picker_changed;

	// Takes the look of the selected box for the defaults when the user has changed it, and saves them when the mouse is up.
	void follow_changes();
	// The controls for when the box shows and how it looks.
	void draw_properties(const TextBox& box);
	// A colour swatch with its label; clicking it opens the picker (with the recent colours). True if `color` was changed.
	bool color_control(const char* label, int which, float color[3]);
	// The undo step round a widget: opened when it is taken hold of, closed when it is let go.
	void begin_edit_if_activated();
	void end_edit_if_deactivated();
	// Sets the style as one undo step, for a control that changes at a click.
	void set_style_once(int id, const TextStyle& style);

	// A drag on the preview: which part of the box is being dragged (-1 for none, otherwise a text_box_geometry::Corner
	// or Edge, or BODY), and the box and the mouse as they were when it began, in fractions of the frame.
	static const int BODY = 8;
	int               m_drag_part;
	text_box_geometry::Rect m_drag_start_box;
	float             m_drag_start_mouse_x;
	float             m_drag_start_mouse_y;
};
