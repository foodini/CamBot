#include "thumbnail_layout.h"

#include <algorithm>
#include <math.h>

namespace thumbnails {

int64_t keyframe_for(const std::vector<int64_t>& keyframes, int64_t ticks, int64_t fallback_step) {
	if (keyframes.empty()) {
		if (fallback_step <= 0 || ticks <= 0)
			return ticks < 0 ? 0 : ticks;
		return (ticks / fallback_step) * fallback_step;
	}
	// The first keyframe after `ticks`; the one before it is the answer.
	size_t low = 0;
	size_t high = keyframes.size();
	while (low < high) {
		const size_t middle = low + (high - low) / 2;
		if (keyframes[middle] <= ticks)
			low = middle + 1;
		else
			high = middle;
	}
	return low == 0 ? keyframes[0] : keyframes[low - 1];
}

std::vector<TileSlot> visible_tiles(double clip_left, double clip_right, double view_left, double view_right, double tile_width, int max_tiles) {
	std::vector<TileSlot> tiles;
	if (tile_width <= 0.0 || max_tiles <= 0 || clip_right <= clip_left)
		return tiles;
	const double from = std::max(clip_left, view_left);
	const double to = std::min(clip_right, view_right);
	if (to <= from)
		return tiles;
	int index = (int)floor((from - clip_left) / tile_width);
	if (index < 0)
		index = 0;
	for (; (int)tiles.size() < max_tiles; index++) {
		TileSlot slot;
		slot.index = index;
		slot.left = clip_left + (double)index * tile_width;
		if (slot.left >= to)
			break;
		slot.right = std::min(slot.left + tile_width, clip_right);
		slot.shown = (slot.right - slot.left) / tile_width;
		tiles.push_back(slot);
	}
	return tiles;
}

int64_t tile_ticks(int64_t clip_in, int64_t clip_out, int index, double tile_width, double ticks_per_pixel) {
	int64_t ticks = clip_in + (int64_t)floor((double)index * tile_width * ticks_per_pixel);
	if (ticks >= clip_out)
		ticks = clip_out - 1;
	if (ticks < clip_in)
		ticks = clip_in;
	return ticks;
}

Catalog::Catalog() : m_keys(), m_entries() {}

size_t Catalog::place_of(const ThumbKey& key) const {
	size_t low = 0;
	size_t high = m_keys.size();
	while (low < high) {
		const size_t middle = low + (high - low) / 2;
		if (m_keys[middle] < key)
			low = middle + 1;
		else
			high = middle;
	}
	return low;
}

bool Catalog::contains(const ThumbKey& key) const {
	const size_t at = place_of(key);
	return at < m_keys.size() && m_keys[at] == key;
}

void Catalog::add(const ThumbKey& key, unsigned texture, int64_t frame) {
	const size_t at = place_of(key);
	Entry entry;
	entry.texture = texture;
	entry.last_used = frame;
	if (at < m_keys.size() && m_keys[at] == key) {
		m_entries[at] = entry;
		return;
	}
	m_keys.insert(m_keys.begin() + at, key);
	m_entries.insert(m_entries.begin() + at, entry);
}

bool Catalog::find(const ThumbKey& key, int64_t frame, unsigned* texture) {
	const size_t at = place_of(key);
	if (at >= m_keys.size() || m_keys[at] != key)
		return false;
	m_entries[at].last_used = frame;
	*texture = m_entries[at].texture;
	return true;
}

bool Catalog::nearest(int media, int64_t ticks, int64_t frame, unsigned* texture) {
	const size_t at = place_of(ThumbKey(media, ticks, false));
	// The candidates are the one at the place and the one before it, if they are of this file.
	int best = -1;
	int64_t best_distance = 0;
	for (int side = 0; side < 2; side++) {
		if (side == 0 && at >= m_keys.size())
			continue;
		if (side == 1 && at == 0)
			continue;
		const size_t index = side == 0 ? at : at - 1;
		if (m_keys[index].media != media)
			continue;
		const int64_t distance = m_keys[index].ticks > ticks ? m_keys[index].ticks - ticks : ticks - m_keys[index].ticks;
		if (best < 0 || distance < best_distance) {
			best = (int)index;
			best_distance = distance;
		}
	}
	if (best < 0)
		return false;
	m_entries[best].last_used = frame;
	*texture = m_entries[best].texture;
	return true;
}

std::vector<unsigned> Catalog::evict(size_t keep, int64_t frame) {
	std::vector<unsigned> removed;
	while (m_keys.size() > keep) {
		// The least recently used of those not used this frame.
		int oldest = -1;
		for (size_t i = 0; i < m_entries.size(); i++) {
			if (m_entries[i].last_used >= frame)
				continue;
			if (oldest < 0 || m_entries[i].last_used < m_entries[oldest].last_used)
				oldest = (int)i;
		}
		if (oldest < 0)
			break;   // Everything left is in use.
		removed.push_back(m_entries[oldest].texture);
		m_keys.erase(m_keys.begin() + oldest);
		m_entries.erase(m_entries.begin() + oldest);
	}
	return removed;
}

}  // namespace thumbnails
