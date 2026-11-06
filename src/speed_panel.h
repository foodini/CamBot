#pragma once

// The Speed section of the left panel: how fast the clip under the playhead (the one outlined on the timeline) plays. Buttons
// for the usual speeds, a field for any other from a tenth of normal speed to sixteen times it, and, once a clip is off normal speed,
// what to do with its sound: stretch it without changing its pitch, change the pitch with the speed like a tape, or silence it.
// A click on a button, or Enter in the field, takes effect at once and is one step of the undo history; every change goes through the
// TimelineEditor.

#include "edit_model.h"

class MediaContainerMgr;
class TimelineEditor;

class SpeedPanel {
public:
	SpeedPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor);

	// Draws the section, as part of the window the caller has open. `can_change` is false while an export runs.
	void draw(bool can_change);

private:
	EditModel*         m_model;
	MediaContainerMgr* m_media;
	TimelineEditor*    m_editor;
	double             m_field;          // What the speed field holds.
	bool               m_field_active;   // The field has the keyboard.
	int                m_field_clip;     // The clip the field's number was for.
};
