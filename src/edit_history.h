#pragma once

// Undo and redo for the edit timeline. An undo step is a copy of what the user's edits change: the
// clips, the transitions between them, the text boxes and paths laid over them, whether the edit has been started (see EditModel::edit_started), and where the playhead
// was, so undoing puts the picture back where it was too. The clip lists are small, so keeping a
// whole copy per step is simpler than recording how to reverse each edit.
//
// Usage: call record() with the state as it was *before* each edit is applied. undo() and redo()
// are given the state as it is now (to keep for the opposite move) and hand back the state to
// restore.

#include <stdint.h>
#include <vector>

#include "edit_model.h"

struct EditState {
	std::vector<Clip>    clips;
	std::vector<Transition> transitions;     // Between them (see EditList); empty, or count + 1 of them.
	std::vector<TextBox> text_boxes;
	std::vector<TrackedPath> paths;
	bool              edit_started;
	int64_t           playhead_ticks;
	EditState() : edit_started(false), playhead_ticks(0) {}
};

class EditHistory {
public:
	static const int MAX_STEPS = 200;   // The oldest are forgotten beyond this.

	EditHistory() {}

	void clear();
	// Remember `before`, the state an edit is about to change. A new edit ends any redo.
	void record(const EditState& before);

	bool can_undo() const { return !m_undo.empty(); }
	bool can_redo() const { return !m_redo.empty(); }
	// Steps back / forward: `current` is kept for the way back, and the state to go to is put in
	// *restored. False, changing nothing, if there is nothing to go to.
	bool undo(const EditState& current, EditState* restored);
	bool redo(const EditState& current, EditState* restored);

private:
	std::vector<EditState> m_undo;   // Oldest first; the last is the next to undo.
	std::vector<EditState> m_redo;   // The last is the next to redo.
};
