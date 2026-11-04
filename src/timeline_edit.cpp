#include "timeline_edit.h"

namespace timeline_edit {

namespace {

// The clip the playhead is in, and the moment in that clip's file its frame comes from.
struct Place {
	int     index;
	int64_t local;
};

bool locate(const EditList& list, int64_t playhead_ticks, Place* place) {
	int index = list.clip_index_for(playhead_ticks);
	if (index < 0)
		return false;
	const Clip& clip = list.clip(index);
	int64_t local = clip.in_ticks + (playhead_ticks - list.start(index));
	if (local < clip.in_ticks)
		local = clip.in_ticks;
	if (local > clip.out_ticks - 1)
		local = clip.out_ticks - 1;
	place->index = index;
	place->local = local;
	return true;
}

// Is there a frame before / after the playhead's in its clip? A frame is a whole one away when
// it is more than about half a frame off, so the first frame (at the clip's start, give or take a
// little) has none before it.
bool frame_before(const Clip& clip, int64_t local, int64_t frame_ticks) {
	return local - clip.in_ticks >= (frame_ticks + 1) / 2;
}

bool frame_after(const Clip& clip, int64_t local, int64_t frame_ticks) {
	return clip.out_ticks - local > frame_ticks + frame_ticks / 2;
}

}  // namespace

int current_clip(const EditList& list, int64_t playhead_ticks) {
	return list.clip_index_for(playhead_ticks);
}

bool can_split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Place place;
	return locate(list, playhead_ticks, &place) && frame_before(list.clip(place.index), place.local, frame_ticks);
}

Result split(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Result result;
	Place place;
	if (!locate(list, playhead_ticks, &place) || !frame_before(list.clip(place.index), place.local, frame_ticks))
		return result;
	const std::vector<Clip>& old_clips = list.clips();
	for (int i = 0; i < (int)old_clips.size(); i++) {
		if (i != place.index) {
			result.clips.push_back(old_clips[i]);
			continue;
		}
		result.clips.push_back(Clip(old_clips[i].media, old_clips[i].in_ticks, place.local));
		result.clips.push_back(Clip(old_clips[i].media, place.local, old_clips[i].out_ticks));
	}
	result.playhead_ticks = playhead_ticks;
	result.ok = true;
	return result;
}

bool can_trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	return can_split(list, playhead_ticks, frame_ticks);
}

Result trim_left(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Result result;
	Place place;
	if (!locate(list, playhead_ticks, &place) || !frame_before(list.clip(place.index), place.local, frame_ticks))
		return result;
	result.clips = list.clips();
	result.clips[place.index].in_ticks = place.local;
	// Everything before this clip is as it was, so the clip still starts where it did, and its
	// first frame is the one the playhead was on.
	result.playhead_ticks = list.start(place.index);
	result.ok = true;
	return result;
}

bool can_trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Place place;
	return locate(list, playhead_ticks, &place) && frame_after(list.clip(place.index), place.local, frame_ticks);
}

Result trim_right(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Result result;
	Place place;
	if (!locate(list, playhead_ticks, &place) || !frame_after(list.clip(place.index), place.local, frame_ticks))
		return result;
	result.clips = list.clips();
	result.clips[place.index].out_ticks = place.local + frame_ticks;   // Up to, not including, the next frame.
	result.playhead_ticks = playhead_ticks;
	result.ok = true;
	return result;
}

bool can_remove(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	(void)frame_ticks;
	return list.count() > 1 && list.clip_index_for(playhead_ticks) >= 0;
}

Result remove_clip(const EditList& list, int64_t playhead_ticks, int64_t frame_ticks) {
	Result result;
	if (!can_remove(list, playhead_ticks, frame_ticks))
		return result;
	const int index = list.clip_index_for(playhead_ticks);
	const std::vector<Clip>& old_clips = list.clips();
	for (int i = 0; i < (int)old_clips.size(); i++) {
		if (i != index)
			result.clips.push_back(old_clips[i]);
	}
	if (index < (int)result.clips.size()) {
		result.playhead_ticks = list.start(index);
	} else {
		int64_t new_total = list.total_ticks() - old_clips[index].duration_ticks();
		result.playhead_ticks = new_total - frame_ticks > 0 ? new_total - frame_ticks : 0;
	}
	result.ok = true;
	return result;
}

}  // namespace timeline_edit
