#include "timeline_selection.h"

void TimelineSelection::note_clips(const EditList& list) {
	if (list.clips() != m_seen) {
		clear();
		m_seen = list.clips();
	}
}

int TimelineSelection::effective_clip(const EditList& list, int64_t playhead_ticks) const {
	int inside = 0;
	int first = -1;
	bool picked_inside = false;
	for (int i = 0; i < list.count(); i++) {
		const int64_t start = list.start(i);
		if (playhead_ticks >= start && playhead_ticks < start + list.clip(i).timeline_ticks()) {
			inside++;
			if (first < 0)
				first = i;
			if (m_kind == CLIP && i == m_clip)
				picked_inside = true;
		}
	}
	if (picked_inside)
		return m_clip;
	if (inside == 1)
		return first;
	if (inside == 0)
		return list.clip_index_for(playhead_ticks);    // Past the end, say: whatever the edit list makes of it.
	return -1;
}
