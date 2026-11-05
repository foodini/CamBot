#include "tile_layer.h"

#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <glad/glad.h>

#include "file_io.h"
#include "stb_image.h"

namespace {

const int    MAX_VISIBLE_TILES = 120;
const int    DISK_LOADS_PER_FRAME = 6;      // Tiles read from the disk and made into textures in one frame.
const int    RESULTS_PER_FRAME = 6;         // ... and ones that came from the web.
const int    STAND_IN_LEVELS = 4;           // How far up to look for a picture to stretch over a tile that hasn't arrived.
const int    MAX_TEXTURES = 400;            // 256 x 256 x 4 bytes each: about 100 MB of video memory at most.
const int    TEXTURES_AFTER_TRIM = 300;
const int    FAILURES_TO_GIVE_UP = 3;
const double MAX_WAIT_SECONDS = 60.0;
const double TOO_MANY_REQUESTS_WAIT = 120.0;

}  // namespace

TileLayer::TileLayer() : m_cache_root(default_cache_root()), m_fetcher(nullptr), m_tiles(), m_prefetch(), m_frame(0), m_texture_count(0) {
	for (int i = 0; i < map_tiles::STYLE_COUNT; i++) {
		m_deep_misses[i] = 0;
		m_use_deep[i] = false;
	}
}

TileLayer::~TileLayer() {
	// (The textures are not deleted here: this can be destroyed after the OpenGL context is, and they go with it.)
	delete m_fetcher;
}

std::string TileLayer::default_cache_root() {
	const char* local = getenv("LOCALAPPDATA");
	if (local != nullptr && local[0] != '\0')
		return fileio::join(fileio::join(local, "CamBot"), "map_tiles");
	const char* home = getenv("HOME");
	if (home != nullptr && home[0] != '\0')
		return fileio::join(fileio::join(fileio::join(home, ".cache"), "CamBot"), "map_tiles");
	return "map_tiles";
}

double TileLayer::now_seconds() const {
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	return std::chrono::duration<double>(now.time_since_epoch()).count();
}

TileLayer::TileId TileLayer::make_id(int style, const map_tiles::TileKey& key) {
	TileId id;
	id.style = style;
	id.key = key;
	return id;
}

TileRequest TileLayer::make_request(const TileId& id, const Tile& tile) const {
	const TileSource& source = map_tiles::source(id.style);
	TileRequest request;
	request.style = id.style;
	request.key = id.key;
	// A tile deeper than the server has ready-made ones of is drawn by the other service, once it has been seen that they aren't there.
	request.deep = source.deep_url_template != nullptr && m_use_deep[id.style] && id.key.z > source.cached_max_zoom;
	request.url = map_tiles::expand_url(request.deep ? source.deep_url_template : source.url_template, id.key);
	request.etag = tile.texture != 0 ? tile.meta.etag : std::string();
	request.picture_path = fileio::join(m_cache_root, map_tiles::cache_path(source.id, id.key));
	request.meta_path = fileio::join(m_cache_root, map_tiles::meta_path(source.id, id.key));
	return request;
}

bool TileLayer::make_texture(const std::string& picture, Tile* tile) {
	int width = 0;
	int height = 0;
	int channels = 0;
	unsigned char* pixels = stbi_load_from_memory((const unsigned char*)picture.data(), (int)picture.size(), &width, &height, &channels, 4);
	if (pixels == nullptr)
		return false;
	unsigned int texture = tile->texture;
	if (texture == 0) {
		glGenTextures(1, &texture);
		m_texture_count++;
	}
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
	stbi_image_free(pixels);
	tile->texture = texture;
	return true;
}

bool TileLayer::load_from_disk(const TileId& id, Tile* tile) {
	const TileSource& source = map_tiles::source(id.style);
	const std::string picture_path = fileio::join(m_cache_root, map_tiles::cache_path(source.id, id.key));
	std::string picture;
	if (!fileio::read_text(picture_path, &picture, nullptr))
		return false;
	if (!make_texture(picture, tile)) {
		// Not a picture (a half-written file, say): forget it and get a new one.
		printf("Map tile %d/%d/%d: the cached picture is unreadable, deleting it\n", id.key.z, id.key.x, id.key.y);
		fileio::delete_file(picture_path);
		fileio::delete_file(fileio::join(m_cache_root, map_tiles::meta_path(source.id, id.key)));
		return false;
	}
	std::string meta_text;
	map_tiles::TileMeta meta;
	if (fileio::read_text(fileio::join(m_cache_root, map_tiles::meta_path(source.id, id.key)), &meta_text, nullptr) &&
			map_tiles::parse_meta(meta_text, &meta)) {
		tile->meta = meta;
	} else {
		// A picture with no details: used as if it had just been fetched (it is kinder to the server than asking again).
		tile->meta.etag.clear();
		tile->meta.fetched = (int64_t)time(nullptr);
	}
	return true;
}

void TileLayer::take_results(int budget) {
	if (m_fetcher == nullptr)
		return;
	TileResult result;
	while (budget > 0 && m_fetcher->next_result(&result)) {
		budget--;
		const TileId id = make_id(result.style, result.key);
		std::map<TileId, Tile>::iterator found = m_tiles.find(id);
		SourceHealth& health = m_health[result.style];
		const char* what = "";
		if (result.outcome == TileResult::LOADED) what = "new picture";
		else if (result.outcome == TileResult::UNCHANGED) what = "unchanged";
		else if (result.outcome == TileResult::MISSING) what = "no picture";
		else what = "failed";
		// How long it took: in line for a worker, until the server began to answer, and in all.
		// (and how many of that source's requests were out when it started: more than two means slow ones were being worked around.)
		char timing[128];
		snprintf(timing, sizeof(timing), "waited %.1f s, started with %d out, reply after %.1f s, done after %.1f s", result.wait_seconds,
			result.out_at_start, result.reply_seconds, result.total_seconds);
		const char* how = result.deep ? ", drawn on request" : "";
		if (result.http_status != 0)
			printf("Map tile %d/%d/%d: HTTP %d, %d bytes (%s%s); %s\n", result.key.z, result.key.x, result.key.y, result.http_status, (int)result.picture.size(), what, how, timing);
		else
			printf("Map tile %d/%d/%d: no reply, WinHTTP error %lu (%s%s); %s\n", result.key.z, result.key.x, result.key.y, result.error_code, what, how, timing);

		// Tiles below the ready-made ones: if the server says it has none (twice), they are asked for in the other way from then on.
		const TileSource& result_source = map_tiles::source(result.style);
		if (result.outcome == TileResult::MISSING && !result.deep && result_source.deep_url_template != nullptr &&
				result.key.z > result_source.cached_max_zoom && !m_use_deep[result.style]) {
			m_deep_misses[result.style]++;
			printf("Map tiles (%s): no ready-made tile at level %d (%d of 2)\n", result_source.name, result.key.z, m_deep_misses[result.style]);
			if (m_deep_misses[result.style] >= 2) {
				m_use_deep[result.style] = true;
				printf("Map tiles (%s): from now on, levels above %d are drawn on request by the imagery service\n", result_source.name, result_source.cached_max_zoom);
				// The tiles that were given up on, and ones waiting with the old address, are asked for afresh.
				for (std::map<TileId, Tile>::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it) {
					if (it->first.style == result.style && it->first.key.z > result_source.cached_max_zoom)
						it->second.missing = false;
				}
				std::vector<std::pair<int, map_tiles::TileKey> > dropped;
				m_fetcher->begin_frame();
				m_fetcher->end_frame(&dropped);
				note_dropped(dropped);
			}
		}
		// (An answer that comes back for the old address after the change says nothing about the new one.)
		const bool stale_deep_miss = result.outcome == TileResult::MISSING && !result.deep && result_source.deep_url_template != nullptr &&
			result.key.z > result_source.cached_max_zoom && m_use_deep[result.style];

		// A prefetched tile is done with, unless the server didn't answer (it is asked for again later).
		if (result.outcome != TileResult::FAILED) {
			for (size_t i = 0; i < m_prefetch.size(); i++) {
				if (m_prefetch[i].style == result.style && m_prefetch[i].key == result.key) {
					m_prefetch.erase(m_prefetch.begin() + i);
					break;
				}
			}
		}

		if (result.outcome == TileResult::FAILED) {
			health.failures++;
			double wait = 2.0;
			for (int i = 1; i < health.failures && wait < MAX_WAIT_SECONDS; i++)
				wait *= 2.0;
			if (wait > MAX_WAIT_SECONDS)
				wait = MAX_WAIT_SECONDS;
			if (result.http_status == 429)
				wait = TOO_MANY_REQUESTS_WAIT;
			health.wait_until = now_seconds() + wait;
			printf("Map tiles (%s): trying again in %.0f seconds\n", map_tiles::source(result.style).name, wait);
		} else {
			health.failures = 0;
			health.wait_until = 0.0;
		}

		if (found == m_tiles.end())
			continue;   // Dropped out of the cache of textures since.
		Tile& tile = found->second;
		tile.pending = false;
		if (result.outcome == TileResult::LOADED) {
			if (make_texture(result.picture, &tile)) {
				tile.meta = result.meta;
				tile.missing = false;
			} else {
				tile.missing = tile.texture == 0;
			}
		} else if (result.outcome == TileResult::UNCHANGED) {
			tile.meta = result.meta;
		} else if (result.outcome == TileResult::MISSING) {
			if (tile.texture == 0 && !stale_deep_miss)
				tile.missing = true;
		}
	}
}

void TileLayer::trim_textures() {
	if (m_texture_count <= MAX_TEXTURES)
		return;
	std::vector<std::pair<uint64_t, TileId> > by_use;
	for (std::map<TileId, Tile>::iterator it = m_tiles.begin(); it != m_tiles.end(); ++it) {
		if (it->second.texture != 0 && it->second.last_used != m_frame)
			by_use.push_back(std::make_pair(it->second.last_used, it->first));
	}
	// Least recently used first (a simple selection: this runs rarely).
	for (size_t i = 0; i < by_use.size() && m_texture_count > TEXTURES_AFTER_TRIM; i++) {
		size_t oldest = i;
		for (size_t j = i + 1; j < by_use.size(); j++) {
			if (by_use[j].first < by_use[oldest].first)
				oldest = j;
		}
		std::swap(by_use[i], by_use[oldest]);
		Tile& tile = m_tiles[by_use[i].second];
		if (tile.pending)
			continue;
		glDeleteTextures(1, &tile.texture);
		m_texture_count--;
		m_tiles.erase(by_use[i].second);
	}
}

void TileLayer::update(int style, const MapViewport& view, std::vector<TileQuad>* quads) {
	quads->clear();
	m_frame++;
	if (style < 0 || style >= map_tiles::STYLE_COUNT)
		return;
	if (m_fetcher == nullptr)
		m_fetcher = new TileFetcher(m_cache_root, TileFetcher::DEFAULT_CACHE_LIMIT_BYTES);
	const TileSource& source = map_tiles::source(style);
	const int z = map_tiles::tile_zoom_for(view.zoom(), source.max_zoom);
	const std::vector<map_tiles::TileKey> wanted = map_tiles::visible_tiles(view, z, MAX_VISIBLE_TILES);
	const double now = now_seconds();
	const bool may_ask = now >= m_health[style].wait_until;
	const int64_t clock = (int64_t)time(nullptr);

	take_results(RESULTS_PER_FRAME);

	m_fetcher->begin_frame();
	int disk_budget = DISK_LOADS_PER_FRAME;
	for (size_t i = 0; i < wanted.size(); i++) {
		const TileId id = make_id(style, wanted[i]);
		Tile& tile = m_tiles[id];
		tile.last_used = m_frame;
		if (tile.texture == 0 && !tile.disk_checked && disk_budget > 0) {
			disk_budget--;
			tile.disk_checked = true;
			if (load_from_disk(id, &tile)) {
				if (map_tiles::refresh_due(tile.meta, clock, map_tiles::AUTOMATIC_REFRESH_AGE)) {
					// Old enough that the server is asked, this once, whether it has changed (the picture is used meanwhile).
				} else {
					tile.revalidated = true;
				}
			}
		}
		if (tile.pending) {
			if (may_ask)
				m_fetcher->want(make_request(id, tile));
			continue;
		}
		if (!may_ask || tile.missing)
			continue;
		if (tile.texture == 0 && tile.disk_checked) {
			tile.pending = true;
			printf("Map tile request %d/%d/%d\n", wanted[i].z, wanted[i].x, wanted[i].y);
			m_fetcher->want(make_request(id, tile));
		} else if (tile.texture != 0 && !tile.revalidated) {
			tile.revalidated = true;
			if (!tile.meta.etag.empty() && map_tiles::refresh_due(tile.meta, clock, map_tiles::AUTOMATIC_REFRESH_AGE)) {
				tile.pending = true;
				printf("Map tile request %d/%d/%d (conditional)\n", wanted[i].z, wanted[i].x, wanted[i].y);
				m_fetcher->want(make_request(id, tile));
			}
		}
	}
	want_prefetch();
	std::vector<std::pair<int, map_tiles::TileKey> > dropped;
	m_fetcher->end_frame(&dropped);
	note_dropped(dropped);

	// What to draw: each tile's own picture, or a part of a bigger tile's in its place.
	for (size_t i = 0; i < wanted.size(); i++) {
		TileQuad quad;
		map_tiles::tile_rect(view, wanted[i], &quad.x0, &quad.y0, &quad.x1, &quad.y1);
		std::map<TileId, Tile>::const_iterator own = m_tiles.find(make_id(style, wanted[i]));
		if (own != m_tiles.end() && own->second.texture != 0) {
			quad.u0 = 0.0f;
			quad.v0 = 0.0f;
			quad.u1 = 1.0f;
			quad.v1 = 1.0f;
			quad.texture = own->second.texture;
			quads->push_back(quad);
			continue;
		}
		for (int up = 1; up <= STAND_IN_LEVELS && up <= wanted[i].z; up++) {
			const map_tiles::StandIn stand_in = map_tiles::ancestor_of(wanted[i], up);
			std::map<TileId, Tile>::const_iterator above = m_tiles.find(make_id(style, stand_in.ancestor));
			if (above != m_tiles.end() && above->second.texture != 0) {
				quad.u0 = stand_in.u0;
				quad.v0 = stand_in.v0;
				quad.u1 = stand_in.u1;
				quad.v1 = stand_in.v1;
				quad.texture = above->second.texture;
				quads->push_back(quad);
				break;
			}
		}
	}
	trim_textures();
}

bool TileLayer::failing(int style) const {
	if (style < 0 || style >= map_tiles::STYLE_COUNT)
		return false;
	return m_health[style].failures >= FAILURES_TO_GIVE_UP;
}

void TileLayer::retry(int style) {
	if (style < 0 || style >= map_tiles::STYLE_COUNT)
		return;
	m_health[style] = SourceHealth();
}

int TileLayer::refresh_visible(int style, const MapViewport& view) {
	if (style < 0 || style >= map_tiles::STYLE_COUNT)
		return 0;
	retry(style);
	const TileSource& source = map_tiles::source(style);
	const int z = map_tiles::tile_zoom_for(view.zoom(), source.max_zoom);
	const std::vector<map_tiles::TileKey> wanted = map_tiles::visible_tiles(view, z, MAX_VISIBLE_TILES);
	const int64_t clock = (int64_t)time(nullptr);
	int count = 0;
	for (size_t i = 0; i < wanted.size(); i++) {
		std::map<TileId, Tile>::iterator found = m_tiles.find(make_id(style, wanted[i]));
		if (found == m_tiles.end())
			continue;
		Tile& tile = found->second;
		if (tile.pending)
			continue;
		if (tile.missing) {
			// Asked again whether the server has a picture now.
			tile.missing = false;
			tile.disk_checked = true;
			count++;
		} else if (tile.texture != 0 && map_tiles::refresh_due(tile.meta, clock, map_tiles::MANUAL_REFRESH_AGE)) {
			// Asked about; update() sends the request (with the ETag if the tile has one, otherwise as a plain fetch).
			tile.pending = true;
			printf("Map tile request %d/%d/%d (refresh)\n", wanted[i].z, wanted[i].x, wanted[i].y);
			count++;
		}
	}
	return count;
}

void TileLayer::stop_requests() {
	if (m_fetcher == nullptr)
		return;
	std::vector<std::pair<int, map_tiles::TileKey> > dropped;
	m_fetcher->begin_frame();
	want_prefetch();
	m_fetcher->end_frame(&dropped);
	note_dropped(dropped);
}

void TileLayer::note_dropped(const std::vector<std::pair<int, map_tiles::TileKey> >& dropped) {
	for (size_t i = 0; i < dropped.size(); i++) {
		std::map<TileId, Tile>::iterator found = m_tiles.find(make_id(dropped[i].first, dropped[i].second));
		if (found != m_tiles.end())
			found->second.pending = false;   // (Asked for again, if it comes back into view.)
	}
}

void TileLayer::prefetch(int style, const std::vector<map_tiles::TileKey>& tiles) {
	if (style < 0 || style >= map_tiles::STYLE_COUNT)
		return;
	if (m_fetcher == nullptr)
		m_fetcher = new TileFetcher(m_cache_root, TileFetcher::DEFAULT_CACHE_LIMIT_BYTES);
	const TileSource& source = map_tiles::source(style);
	int already = 0;
	for (size_t i = 0; i < tiles.size(); i++) {
		if (fileio::file_exists(fileio::join(m_cache_root, map_tiles::cache_path(source.id, tiles[i])))) {
			already++;
			continue;
		}
		PrefetchItem item;
		item.style = style;
		item.key = tiles[i];
		item.announced = false;
		m_prefetch.push_back(item);
	}
	printf("Map tiles: getting a head start on the launch area: %d tiles to fetch, %d already cached\n", (int)m_prefetch.size(), already);
}

void TileLayer::want_prefetch() {
	if (m_prefetch.empty())
		return;
	// Giving up on the head start if the server isn't answering: the map asks for what it needs when it is shown.
	for (size_t i = 0; i < m_prefetch.size();) {
		if (failing(m_prefetch[i].style))
			m_prefetch.erase(m_prefetch.begin() + i);
		else
			i++;
	}
	const double now = now_seconds();
	for (size_t i = 0; i < m_prefetch.size(); i++) {
		if (now < m_health[m_prefetch[i].style].wait_until)
			continue;
		PrefetchItem& item = m_prefetch[i];
		if (!item.announced) {
			item.announced = true;
			printf("Map tile request %d/%d/%d (head start)\n", item.key.z, item.key.x, item.key.y);
		}
		m_fetcher->want(make_request(make_id(item.style, item.key), Tile()));
	}
}

void TileLayer::service() {
	if (m_fetcher == nullptr)
		return;
	take_results(RESULTS_PER_FRAME);
	if (m_prefetch.empty())
		return;
	std::vector<std::pair<int, map_tiles::TileKey> > dropped;
	m_fetcher->begin_frame();
	want_prefetch();
	m_fetcher->end_frame(&dropped);
	note_dropped(dropped);
}

int TileLayer::busy_count() {
	return m_fetcher == nullptr ? 0 : m_fetcher->busy_count();
}
