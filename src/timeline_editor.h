#pragma once

// Carries out the user's edits of the timeline: Split, Trim Left, Trim Right and Delete at the
// playhead (what each does is in timeline_edit.h), and Undo and Redo of them. The arithmetic is
// in timeline_edit.cpp and the history in edit_history.cpp; this ties them to the EditModel (which
// remembers the clips and goes into the project file) and the video being played (which has to be
// told, and shows the frame the playhead ends up on).
//
// Each edit pauses playback, and none is done while an export is running. The "can_" questions are
// cheap enough to ask every frame, to grey out buttons.

#include <string>

#include "edit_history.h"
#include "edit_model.h"
#include "text_box.h"

class MediaContainerMgr;

class TimelineEditor {
public:
	TimelineEditor() : m_box_edit_pending(false) {}

	bool can_split(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_left(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_right(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_remove(const EditModel& model, const MediaContainerMgr& media) const;
	// Whether the clip under the playhead could be given this speed and treatment of its sound (see timeline_edit::set_speed).
	bool can_set_speed(const EditModel& model, const MediaContainerMgr& media, double speed, int audio_mode) const;
	bool can_undo(const MediaContainerMgr& media) const;
	bool can_redo(const MediaContainerMgr& media) const;

	// Each returns whether anything was done.
	bool split(EditModel* model, MediaContainerMgr* media);
	bool trim_left(EditModel* model, MediaContainerMgr* media);
	bool trim_right(EditModel* model, MediaContainerMgr* media);
	bool remove_clip(EditModel* model, MediaContainerMgr* media);
	// Moves clip `from` so that it is clip `to` afterwards (see timeline_edit::move_clip); the picture
	// on screen stays the same. Undoable like the cuts.
	bool move_clip(EditModel* model, MediaContainerMgr* media, int from, int to);
	// Plays the clip under the playhead at `speed` (an AudioMode says what happens to its sound), as one undoable step. The picture
	// on screen stays the same frame.
	bool set_clip_speed(EditModel* model, MediaContainerMgr* media, double speed, int audio_mode);
	bool undo(EditModel* model, MediaContainerMgr* media);
	bool redo(EditModel* model, MediaContainerMgr* media);

	// ---- Text boxes (see text_box.h) ----
	// Adding and removing one are undoable steps like the cuts (and, unlike them, don't count as starting to edit the video).
	// add_text_box gives the id the box was given, or 0 if it couldn't be added.
	int  add_text_box(EditModel* model, MediaContainerMgr* media, const TextBox& box);
	bool remove_text_box(EditModel* model, MediaContainerMgr* media, int id);
	// Moves the playhead to where the box starts to show in full (the end of its fade in), on the exact frame. Not an edit, so it
	// is not undone.
	void go_to_text_box(const EditModel& model, MediaContainerMgr* media, int id);

	// Typing in a text box, or dragging it about the picture, is one undo step however many changes it takes: call
	// begin_box_edit() when the typing field is entered or the drag starts, set_text() / set_geometry() for each change (the
	// picture follows at once), and end_box_edit() when the field is left or the drag ends.
	void begin_box_edit(const EditModel& model, const MediaContainerMgr& media);
	void set_text(EditModel* model, int id, const std::string& text);
	// Where the box is, as fractions of the frame (see text_box.h).
	void set_geometry(EditModel* model, int id, float x, float y, float w, float h);
	// How the words look (see TextStyle).
	void set_style(EditModel* model, int id, const TextStyle& style);
	// When it shows: the frame it begins on, and the lengths of its fade in, dwell and fade out.
	void set_timing(EditModel* model, int id, const FileTime& anchor, int64_t fade_in_ticks, int64_t dwell_ticks, int64_t fade_out_ticks);
	void end_box_edit(const EditModel& model, const MediaContainerMgr& media);

private:
	enum Operation { SPLIT, TRIM_LEFT, TRIM_RIGHT, REMOVE };
	bool edit(Operation operation, EditModel* model, MediaContainerMgr* media);
	// Makes `state` the one being played and edited. False, changing nothing, if it can't be.
	bool apply_state(const EditState& state, EditModel* model, MediaContainerMgr* media);

	// Makes the typing in progress (if any) a step of the history.
	void commit_box_edit(const EditModel& model, const MediaContainerMgr& media);

	EditHistory m_history;
	bool        m_box_edit_pending;
	EditState   m_box_edit_before;    // The state when the edit began.
};
