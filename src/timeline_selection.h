#pragma once

// What the user has picked on the timeline: a clip, or a transition (the join between two clips, or the start or end of the
// video; see seam numbers below), or nothing. Picking is done by clicking; the text boxes have a selection of their own
// (see text_panel.h).
//
// With nothing picked, the clip the playhead is in is the one the edits (Split, Trim and so on) and the Speed section act on, as
// they always did. A clip that has been clicked stays the one for as long as the playhead is somewhere inside it; once the
// playhead has left it, the clip the playhead is in is the one again. Where the playhead is in more than one clip at once
// (while one fades into the next) and none of them is picked, there is no one clip to act on, and effective_clip() says so
// with -1, so the buttons can be greyed out instead of guessing.
//
// A change to the clips themselves (a cut, a move, an undo) lets go of the pick, as the numbers it was made with no longer mean
// the same clips.
//
// Seam numbers: 0 is the start of the video, 1 to count-1 the joins (seam i is between clip i-1 and clip i), count the end of
// the video.

#include <stdint.h>
#include <vector>

#include "edit_model.h"

class TimelineSelection {
public:
	enum Kind { NOTHING, CLIP, TRANSITION };

	TimelineSelection() : m_kind(NOTHING), m_clip(-1), m_seam(-1) {}

	Kind kind() const { return m_kind; }
	// The clip that is picked (-1 unless kind() is CLIP) and the seam (-1 unless it is TRANSITION).
	int  picked_clip() const { return m_kind == CLIP ? m_clip : -1; }
	int  picked_seam() const { return m_kind == TRANSITION ? m_seam : -1; }

	void select_clip(int index) { m_kind = CLIP; m_clip = index; m_seam = -1; }
	void select_transition(int seam) { m_kind = TRANSITION; m_seam = seam; m_clip = -1; }
	void clear() { m_kind = NOTHING; m_clip = -1; m_seam = -1; }

	// For an edit that has just changed the clips and is picking something of its own (the join a Split has made, say): takes the
	// clips as they now are as the ones the pick was made with, so that the next note_clips() doesn't let go of it.
	void adopt_clips(const EditList& list) { m_seen = list.clips(); }

	// Call each frame with the clips as they are: lets go of the pick if the clips have changed since the last call.
	void note_clips(const EditList& list);

	// The one clip the edits act on with the playhead at `playhead_ticks`, or -1 if there is none (no clips, or the playhead
	// is in several and none of them is picked).
	int effective_clip(const EditList& list, int64_t playhead_ticks) const;

private:
	Kind              m_kind;
	int               m_clip;
	int               m_seam;
	std::vector<Clip> m_seen;     // The clips as of the last note_clips().
};
