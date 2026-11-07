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
#include "timeline_selection.h"

class MediaContainerMgr;

class TimelineEditor {
public:
	TimelineEditor() : m_selection(nullptr), m_box_edit_pending(false) {}

	// What is picked on the timeline (see timeline_selection.h). The cuts and the speed act on the clip picked, if one is, and
	// otherwise on the clip the playhead belongs to; a trim or a split picks the transition it has made or moved. Without one,
	// the clip the playhead belongs to is always the one.
	void set_selection(TimelineSelection* selection) { m_selection = selection; }
	// The clip an edit would act on now: -1 if there are no clips.
	int  target_clip(const EditModel& model, const MediaContainerMgr& media) const;

	// Whether the video is playing. The cuts and the speed are not done then, and their buttons are off: where the playhead is as it
	// plays is no way to choose a frame. A key for one of them pauses the video and says so instead (see EnvConfig::say).
	bool playing() const;

	bool can_split(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_left(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_trim_right(const EditModel& model, const MediaContainerMgr& media) const;
	bool can_remove(const EditModel& model, const MediaContainerMgr& media) const;
	// Whether the clip under the playhead could be given this speed and treatment of its sound (see timeline_edit::set_speed).
	bool can_set_speed(const EditModel& model, const MediaContainerMgr& media, double speed, int audio_mode) const;
	bool can_undo(const MediaContainerMgr& media) const;
	bool can_redo(const MediaContainerMgr& media) const;

	// Each returns whether anything was done.
	// A split picks the join it has made (in `selection`, if one is given), so that its transition is at once the one being edited.
	bool split(EditModel* model, MediaContainerMgr* media);
	// A trim picks the transition at the edge it has moved (the clip's start for Trim Left, its end for Trim Right).
	bool trim_left(EditModel* model, MediaContainerMgr* media);
	bool trim_right(EditModel* model, MediaContainerMgr* media);
	bool remove_clip(EditModel* model, MediaContainerMgr* media);
	// Moves clip `from` so that it is clip `to` afterwards (see timeline_edit::move_clip); the picture
	// on screen stays the same. Undoable like the cuts.
	bool move_clip(EditModel* model, MediaContainerMgr* media, int from, int to);
	// Plays the clip under the playhead at `speed` (an AudioMode says what happens to its sound), as one undoable step. The picture
	// on screen stays the same frame.
	bool set_clip_speed(EditModel* model, MediaContainerMgr* media, double speed, int audio_mode);
	// Gives seam `seam` (see EditList) a transition of `type` lasting `ticks` of the timeline, as one undoable step; a cut (or no
	// ticks) takes the transition away. False, changing nothing, if that is no change or is not allowed (see EditList::transitions_valid).
	bool set_transition(EditModel* model, MediaContainerMgr* media, int seam, int type, int64_t ticks);
	// Gives every transition of the kind `type` a transition of that type lasting `ticks` (or as long as the seam allows, if that is
	// less; whole frames), as one undoable step: for TRANSITION_CROSSFADE the joins between clips, for TRANSITION_FADE the start and the
	// end of the video. With `only_unset` the ones that have no transition now are given one and the others left as they are. Joins where
	// the footage carries straight on (EditModel::join_is_continuous) are left alone. Returns how many seams were changed.
	int  set_all_transitions(EditModel* model, MediaContainerMgr* media, int type, int64_t ticks, bool only_unset);
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
	// If the video is playing, pauses it and tells the user that `what` was not done; true if it was playing.
	bool stop_for_edit(const char* what);
	enum Operation { SPLIT, TRIM_LEFT, TRIM_RIGHT, REMOVE };
	bool edit(Operation operation, EditModel* model, MediaContainerMgr* media);
	// Makes `state` the one being played and edited. False, changing nothing, if it can't be.
	bool apply_state(const EditState& state, EditModel* model, MediaContainerMgr* media);

	// Makes the typing in progress (if any) a step of the history.
	void commit_box_edit(const EditModel& model, const MediaContainerMgr& media);

	EditHistory        m_history;
	TimelineSelection* m_selection;
	bool        m_box_edit_pending;
	EditState   m_box_edit_before;    // The state when the edit began.
};
