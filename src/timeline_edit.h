#pragma once

// The cuts a user can make on the edit timeline, as plain arithmetic on a list of clips: no
// OpenGL, no ImGui, no decoder, so they can be tested. Each takes the clips as they are and where
// the playhead is, and says what the clips (and the playhead) should be afterwards.
//
// The playhead is a frame. The frame under it is kept by every operation here: Split puts it at
// the start of the right-hand piece, Trim Left and Trim Right throw away what is before / after
// it and keep it, and Delete removes the whole clip it is in.
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
	int64_t           playhead_ticks;  // Where the playhead should be afterwards (timeline ticks of the new clips).
	Result() : ok(false), playhead_ticks(0) {}
};

// Cut the clip under the playhead in two, the playhead's frame being the first of the second.
// Not possible on a clip's first frame (the cut would be at its very start).
bool   can_split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);
Result split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);

// Throw away the part of the clip before the playhead's frame; what follows moves up to close
// the gap. The playhead stays on its frame, which is now the first of the clip. Not possible on
// a clip's first frame.
bool   can_trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);
Result trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);

// Throw away the part of the clip after the playhead's frame; what follows moves up to close the
// gap. The playhead stays on its frame, which is now the last of the clip. Not possible on a
// clip's last frame.
bool   can_trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);
Result trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);

// Remove the clip under the playhead altogether, closing the gap. The playhead goes to where the
// clip began, which is the start of whatever moved into its place (or, if it was the last clip,
// the last frame there now is). The only clip left can't be removed.
bool   can_remove(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);
Result remove_clip(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks);

// Which clip the playhead is in, or -1 when there are none. The same answer the timeline uses to
// show the "current" clip.
int    current_clip(const EditList& list, int64_t playhead_ticks);

}  // namespace timeline_edit
