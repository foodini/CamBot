#pragma once

// The cuts a user can make on the edit timeline, as plain arithmetic on a list of clips: no
// OpenGL, no ImGui, no decoder, so they can be tested. Each takes the clips as they are and where
// the playhead is, and says what the clips (and the playhead) should be afterwards.
//
// The playhead is a frame. The frame under it is kept by every operation here: Split puts it at
// the start of the right-hand piece, Trim Left and Trim Right throw away what is before / after
// it and keep it, and Delete removes the whole clip it is in.
//
// The transitions go with the clips (see edit_model.h). The start and the end of the video keep theirs whatever is done. A join keeps
// its transition if the same two clips are still next to each other, in the same order; a join made by an edit (the one a split
// makes, the one a removed clip leaves, the ones a move makes) is a plain cut. Split, the trims and a change of speed are not
// possible when they would leave a transition longer than half of a clip next to it (the clip would no longer have room for it); a
// removal or a move that leaves one too long for its new neighbour shortens it to fit.
//
// Each edit is for one clip: the one the playhead belongs to (the one showing more of itself, in a crossfade), or, when the caller
// names one (`clip`, the number of a clip the playhead is in, for the clip that has been picked), that one. A named clip the playhead
// is not in can't be edited. Times and frames are judged in the clip that is edited.
//
// All times are the timeline's ticks (see media_decoder.h); `frame_ticks` is how long one frame
// lasts. Which frame is "the first" or "the last" of a clip is judged to within half a frame, the
// way the decoder matches frames to clips.

#include <stdint.h>
#include <vector>

#include "edit_model.h"

namespace timeline_edit {

struct Result {
	bool              ok;              // False if the operation can't be done here; nothing else is set then.
	std::vector<Clip> clips;           // The clips afterwards.
	std::vector<Transition> transitions;   // And the transitions between them (count + 1; see EditList).
	int64_t           playhead_ticks;  // Where the playhead should be afterwards (timeline ticks of the new clips).
	Result() : ok(false), playhead_ticks(0) {}
};

// Cut the clip under the playhead in two, the playhead's frame being the first of the second.
// Not possible on a clip's first frame (the cut would be at its very start).
bool   can_split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);
Result split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);

// Throw away the part of the clip before the playhead's frame; what follows moves up to close
// the gap. The playhead stays on its frame, which is now the first of the clip. Not possible on
// a clip's first frame.
bool   can_trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);
Result trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);

// Throw away the part of the clip after the playhead's frame; what follows moves up to close the
// gap. The playhead stays on its frame, which is now the last of the clip. Not possible on a
// clip's last frame.
bool   can_trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);
Result trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);

// Remove the clip under the playhead altogether, closing the gap. The playhead goes to where the
// clip began, which is the start of whatever moved into its place (or, if it was the last clip,
// the last frame there now is). The only clip left can't be removed.
bool   can_remove(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);
Result remove_clip(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks, int clip = -1);

// Moves clip `from` so that it is clip `to` afterwards (both 0 .. count-1; `to` counts the list as it
// will be, with the moved clip in it). The playhead stays on the same frame of the same piece of
// footage, which has moved with its clip, so the picture doesn't change. Not possible when `to` is
// where the clip already is. This is also what reordering the files in the sync view is: there
// each clip is a whole file.
bool   can_move(const EditList& list, int from, int to);
Result move_clip(const EditList& list, int from, int to, int64_t playhead_ticks);

// While a clip is being dragged: where it would go if dropped with the mouse at `position_ticks`
// (a timeline time). The answer is a `to` for move_clip(): the number of the other clips that are
// centered before the mouse.
int     drop_index(const EditList& list, int dragged, double position_ticks);
// Where to draw the mark that shows the drop: the timeline time of the right-hand edge of the
// clip the dragged one would go after (not counting the dragged clip itself), or 0 if it would go first.
int64_t drop_marker_ticks(const EditList& list, int dragged, int to);

// Plays the clip under the playhead at `speed` times normal speed (MIN_SPEED to MAX_SPEED), its sound treated as `audio_mode` (an
// AudioMode). The clip lasts that much longer or shorter on the timeline, and what follows moves to suit. The playhead stays on
// the same frame of the footage. Not possible when there is no clip, when the speed is out of range, or when it changes
// nothing.
bool   can_set_speed(const EditList& list, int64_t playhead_ticks, double speed, int audio_mode, int clip = -1);
Result set_speed(const EditList& list, int64_t playhead_ticks, double speed, int audio_mode, int64_t frame_ticks, int clip = -1);

// Where the playhead should be so that it stays on the same frame of the same clip when only the transitions between the clips have
// changed from `before` to `after` (the clips being the same, so a crossfade made or lengthened pulls the clips after it earlier).
int64_t same_frame_after(const EditList& before, const EditList& after, int64_t playhead_ticks);

// Which clip the playhead is in, or -1 when there are none. The same answer the timeline uses to
// show the "current" clip.
int    current_clip(const EditList& list, int64_t playhead_ticks);

}  // namespace timeline_edit
