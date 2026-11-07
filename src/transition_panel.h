#pragma once

// The Transition section of the left panel: the transition picked on the timeline (by clicking its icon, see seam_icon.h), where
// its type and its length are set. At the start and the end of the video the type is None or a Fade (from black, or to black, over the
// whole picture with its overlays and text, and the sound); at a join between two clips it is None or a Crossfade (the end of one clip
// laid over the start of the next). Under that, two buttons give that transition to all the others of its kind at once.

#include "edit_model.h"

class MediaContainerMgr;
class TimelineEditor;
class TimelineSelection;

class TransitionPanel {
public:
	TransitionPanel(EditModel* model, MediaContainerMgr* media, TimelineEditor* editor, TimelineSelection* selection);

	// Draws the section, as part of the window the caller has open. `can_change` is false while an export runs.
	void draw(bool can_change);

private:
	// The transition picked: its type and length.
	void draw_picked(int seam, bool can_change);
	// The two buttons that give the picked transition's type and length to all the transitions of its kind (every join, or the
	// start and the end), or only to those that have none.
	void draw_apply_buttons(int seam, const Transition& now, bool can_change);
	// The length a transition gets when it is first chosen: a second if the video allows, else as long as it may be. Whole frames.
	int64_t default_ticks(int seam) const;
	// `seconds` as whole frames of the timeline, at least one and at most what the seam allows (0 if it allows none).
	int64_t seconds_to_ticks(int seam, double seconds) const;

	EditModel*         m_model;
	MediaContainerMgr* m_media;
	TimelineEditor*    m_editor;
	TimelineSelection* m_selection;
	int                m_last_seam;     // The seam that was picked last frame, to notice a new pick.
	double             m_field;         // The length being typed, in seconds.
	bool               m_field_active;
	int                m_field_seam;
	int                m_all_count;     // How many transitions the last press changed, to say so (-1: no press yet).
};
