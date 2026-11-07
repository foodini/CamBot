#include "timeline_selection.h"

void TimelineSelection::note_clips(const EditList& list) {
	if (list.clips() != m_seen) {
		clear();
		m_seen = list.clips();
	}
}

int TimelineSelection::effective_clip(const EditList& list, int64_t playhead_ticks) const {
	if (list.count() == 0)
		return -1;
	if (m_kind == CLIP && m_clip >= 0 && m_clip < list.count() && playhead_ticks >= list.start(m_clip) && playhead_ticks < list.end(m_clip))
		return m_clip;
	// Not picked, or the playhead has left it: the clip the playhead belongs to (in a crossfade, the one showing more of itself).
	return list.clip_index_for(playhead_ticks);
}
