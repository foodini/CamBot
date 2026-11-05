#pragma once

// The arithmetic and bookkeeping behind the pictures on the timeline's clips (see thumbnail_store.h
// for the part that gets them from the video and draws with them): where along a clip each picture
// goes, which moment of the video it shows, which keyframe of the file that comes to, and which
// of the pictures already in hand to show while a better one is on its way. Plain code with no
// FFmpeg, OpenGL or ImGui in it, so it can be tested.
//
// Along a clip the pictures are laid out from its left end, one after another: the first against
// the clip's left edge, the next where the first one stops, and so on until the clip runs out of
// room, where the last is cut off short. Each shows the moment of the clip that is at its own left
// edge. That puts the pictures in the same places however the timeline is scrolled, and what they
// show changes as it is zoomed.

#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace thumbnails {

// Which picture: a moment of one media file, and whether it is that exact frame or the keyframe
// the file has at or before it (a picture of a keyframe is the same wherever on the timeline it is
// wanted from, so it can be used again; it is also much quicker to get).
struct ThumbKey {
	int     media;
	int64_t ticks;    // From the start of the media file.
	bool    exact;
	ThumbKey() : media(0), ticks(0), exact(false) {}
	ThumbKey(int media_index, int64_t moment, bool exact_frame) : media(media_index), ticks(moment), exact(exact_frame) {}
	bool operator==(const ThumbKey& other) const { return media == other.media && ticks == other.ticks && exact == other.exact; }
	bool operator!=(const ThumbKey& other) const { return !(*this == other); }
	bool operator<(const ThumbKey& other) const {
		if (media != other.media) return media < other.media;
		if (ticks != other.ticks) return ticks < other.ticks;
		return (int)exact < (int)other.exact;
	}
};

// The keyframe at or before `ticks`, out of `keyframes` (the times of the file's keyframes, in order),
// or the first of them if `ticks` is before them all. When the file has no list of them (`keyframes`
// is empty) it is `ticks` rounded down to a multiple of `fallback_step`, so the requests for a
// stretch of the file still come to a few different pictures and not one each.
int64_t keyframe_for(const std::vector<int64_t>& keyframes, int64_t ticks, int64_t fallback_step);

// One picture's place along a clip.
struct TileSlot {
	int    index;      // 0 for the first, against the clip's left edge.
	double left;       // In pixels, as the clip's ends are given. Not held to what is in view.
	double right;      // The picture's own width on from `left`, or less, where the clip ends first.
	double shown;      // How much of the picture shows, 0 to 1, of its width from the left: less than 1 for the last one.
	TileSlot() : index(0), left(0.0), right(0.0), shown(1.0) {}
};

// The pictures of a clip that lie, in part, between `view_left` and `view_right`. The clip runs
// from `clip_left` to `clip_right`, either of which may be far outside the view when zoomed in;
// each picture is `tile_width` wide. At most `max_tiles` come back (the first of them, from the left).
std::vector<TileSlot> visible_tiles(double clip_left, double clip_right, double view_left, double view_right, double tile_width, int max_tiles);

// The moment of a clip that tile `index` shows: its left edge, which is `index * tile_width` pixels of
// `ticks_per_pixel` from the start of the clip. Kept inside the clip (never past its last tick).
int64_t tile_ticks(int64_t clip_in, int64_t clip_out, int index, double tile_width, double ticks_per_pixel);

// The pictures that have arrived and are being kept, by what they show, and which of them to throw
// away when there are too many. A picture is "used" when it is asked for in a frame, which keeps
// it; the ones not used for the longest time go first. (The pictures themselves are OpenGL
// textures, held here as their numbers.)
class Catalog {
public:
	Catalog();

	void   add(const ThumbKey& key, unsigned texture, int64_t frame);
	// The picture for exactly this, if it is here; counts as used in `frame`.
	bool   find(const ThumbKey& key, int64_t frame, unsigned* texture);
	// The picture of the same file nearest in time to `ticks`, if there is one; counts as used in `frame`.
	bool   nearest(int media, int64_t ticks, int64_t frame, unsigned* texture);
	bool   contains(const ThumbKey& key) const;
	size_t count() const { return m_keys.size(); }

	// Removes the least recently used pictures until at most `keep` remain, never any used in `frame`,
	// and returns their textures, so whoever owns them can delete them.
	std::vector<unsigned> evict(size_t keep, int64_t frame);

private:
	struct Entry {
		unsigned texture;
		int64_t  last_used;
	};
	// Sorted by key, with the entries in the same places (a sorted list is enough for the few hundred pictures there are).
	std::vector<ThumbKey> m_keys;
	std::vector<Entry>    m_entries;

	// Where `key` is, or would go.
	size_t place_of(const ThumbKey& key) const;
};

}  // namespace thumbnails
