#pragma once

// Carries out the user's edits of the timeline: Split, Trim Left, Trim Right and Delete at the
// playhead (what each does is in timeline_edit.h), and Undo and Redo of them. The arithmetic is
// in timeline_edit.cpp and the history in edit_history.cpp; this ties them to the EditModel (which
// remembers the clips and goes into the project file) and the video being played (which has to be
// told, and shows the frame the playhead ends up on).
//
// Each edit pauses playback, and none is done while an export is running. The "can_" questions are
// cheap enough to ask every frame, to grey out buttons.

#include "edit_history.h"
#include "edit_model.h"

class MediaContainerMgr;

class TimelineEditor {
public:
	TimelineEditor() {}

	bool can_split(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_left(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_right(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_remove(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_undo(const MediaContainerMgr& media) const;
	bool can_redo(const MediaContainerMgr& media) const;

	// Each returns whether anything was done.
	bool split(EditModel* model, MediaContainerMgr* media);
	bool trim_left(EditModel* model, MediaContainerMgr* media);
	bool trim_right(EditModel* model, MediaContainerMgr* media);
	bool remove_clip(EditModel* model, MediaContainerMgr* media);
	bool undo(EditModel* model, MediaContainerMgr* media);
	bool redo(EditModel* model, MediaContainerMgr* media);

private:
	enum Operation { SPLIT, TRIM_LEFT, TRIM_RIGHT, REMOVE };
	bool edit(Operation operation, EditModel* model, MediaContainerMgr* media);
	// Makes `state` the one being played and edited. False, changing nothing, if it can't be.
	bool apply_state(const EditState& state, EditModel* model, MediaContainerMgr* media);

	EditHistory m_history;
};
