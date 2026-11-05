#pragma once

// The map's picture background: keeps the tiles the pane needs (from the disk cache when it can,
// from the web when it must; see tile_fetcher.h), turns them into OpenGL textures, and says what to
// draw. It doesn't draw anything itself: update() gives a list of rectangles with a texture and the
// part of it to show in each, for the caller to put on the screen with whatever it draws with.
//
// While a tile is on its way, the picture of a tile that covers it from a lower zoom level (a few
// levels up, if there is one) is stretched in its place, so zooming in is blurry for a moment instead
// of blank.
//
// Nothing here waits on the network or the disk for long: at most a few tiles are read from the disk
// and turned into textures in a frame. All of it is for the main thread (the one with the OpenGL
// context), except what TileFetcher does on its own threads.

#include <map>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "map_tiles.h"
#include "map_view.h"
#include "tile_fetcher.h"

// One piece of the picture: where it goes in the pane (pixels from its top left corner) and what part
// of the texture goes there (0..1 each way).
struct TileQuad {
	float        x0;
	float        y0;
	float        x1;
	float        y1;
	float        u0;
	float        v0;
	float        u1;
	float        v1;
	unsigned int texture;
};

class TileLayer {
public:
	TileLayer();
	~TileLayer();

	// Brings the tiles for `view` up to date and fills *quads with what to draw, in order. Call once a
	// frame while the map is showing.
	void update(int style, const MapViewport& view, std::vector<TileQuad>* quads);

	// A head start: fetches these tiles into the disk cache (the ones not already there) without waiting
	// for the map to be shown, so they are on hand when it is. It makes no textures. Needs service() called
	// every frame the map isn't showing (update() does the same work while it is).
	void prefetch(int style, const std::vector<map_tiles::TileKey>& tiles);
	// Takes what has come back and keeps asking for the tiles of prefetch(). For frames update() isn't called in.
	void service();

	// Whether the last answers from this source have been failures (no answer, or "try later"), enough
	// of them in a row that it is fair to say it isn't working. A tile coming through clears it.
	bool failing(int style) const;
	// Forgets that a source was failing, and the waiting out that goes with it, so it is tried afresh.
	void retry(int style);

	// Asks the server whether the tiles now in view that were last fetched more than a week ago have
	// changed, and about any that it said it had no picture for. Returns how many it asked about.
	int refresh_visible(int style, const MapViewport& view);

	// Drops the requests that are still waiting (for when the map is no longer on screen). Ones already out finish.
	void stop_requests();

	// How many requests are waiting or out.
	int busy_count();

	// The folder the cache is in: %LOCALAPPDATA%\CamBot\map_tiles.
	static std::string default_cache_root();

private:
	struct TileId {
		int                style;
		map_tiles::TileKey key;
		bool operator<(const TileId& other) const {
			if (style != other.style) return style < other.style;
			return key < other.key;
		}
	};
	struct Tile {
		unsigned int        texture;        // 0: none (yet).
		bool                disk_checked;   // The disk cache has been looked in.
		bool                pending;        // A request for it is waiting or out.
		bool                missing;        // The server has no picture for it; not asked again.
		bool                revalidated;    // Asked whether it has changed, this run (or the answer is not wanted).
		map_tiles::TileMeta meta;
		uint64_t            last_used;      // The frame it was last wanted in.
		Tile() : texture(0), disk_checked(false), pending(false), missing(false), revalidated(false), meta(), last_used(0) {}
	};

	struct PrefetchItem {
		int                style;
		map_tiles::TileKey key;
		bool               announced;
	};

	struct SourceHealth {
		int    failures;         // In a row.
		double wait_until;       // Seconds on the program's clock: no new requests to this source before then.
		SourceHealth() : failures(0), wait_until(0.0) {}
	};

	static TileId make_id(int style, const map_tiles::TileKey& key);
	TileRequest make_request(const TileId& id, const Tile& tile) const;
	// Reads a tile from the disk cache. True if there was a picture.
	bool load_from_disk(const TileId& id, Tile* tile);
	// Makes a texture of a picture that has been read (png or jpeg). True if it could.
	bool make_texture(const std::string& picture, Tile* tile);
	void take_results(int budget);
	// Asks for the prefetch tiles still to come (inside a begin_frame()/end_frame() of the fetcher).
	void want_prefetch();
	// Notes that the fetcher dropped these (style, key) requests.
	void note_dropped(const std::vector<std::pair<int, map_tiles::TileKey> >& dropped);
	void trim_textures();
	double now_seconds() const;

	std::string                m_cache_root;
	TileFetcher*               m_fetcher;
	std::map<TileId, Tile>     m_tiles;
	std::vector<PrefetchItem>  m_prefetch;   // Still to be fetched.
	// For a source with a deep_url_template: how many times a tile deeper than its ready-made ones was found not to be
	// there, and whether the deep address has been used since (from then on, for the rest of the run).
	int                        m_deep_misses[map_tiles::STYLE_COUNT];
	bool                       m_use_deep[map_tiles::STYLE_COUNT];
	SourceHealth               m_health[map_tiles::STYLE_COUNT];
	uint64_t                   m_frame;
	int                        m_texture_count;
};
