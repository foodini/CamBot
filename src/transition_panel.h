#pragma once

// The Transition section of the left panel: the transition picked on the timeline (by clicking its icon, see seam_icon.h), where
// its type, its length and whatever else belongs to the type are set. For now it only says which transition is picked and that
// it is a plain cut; the types come next.

#include "edit_model.h"

class TimelineSelection;

class TransitionPanel {
public:
	TransitionPanel(EditModel* model, TimelineSelection* selection);

	// Draws the section, as part of the window the caller has open. `can_change` is false while an export runs.
	void draw(bool can_change);

private:
	EditModel*         m_model;
	TimelineSelection* m_selection;
	int                m_last_seam;     // The seam that was picked last frame, to notice a new pick.
};
