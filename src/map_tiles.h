#pragma once

// The arithmetic and rules behind the map's picture background (see tile_layer.h for the part that
// fetches and draws it): which web map tiles are needed to cover the pane, where each lands on the
// screen, which tile to stand in for one that hasn't arrived, how the tiles are named on the disk,
// and when a tile that is already on the disk is old enough to ask the server about again. Plain
// code with no network, OpenGL or ImGui in it, so it can be tested.
//
// Tiles are the usual "slippy map" ones: at zoom level z the world is 2^z tiles across and down,
// tile (x, y) counting from the top left (northwest) corner, each 256 pixels square at its own
// level. That is the same projection map_view.h's world coordinates use.

#include <stdint.h>
#include <string>
#include <vector>

#include "map_view.h"

// A place a picture can come from.
struct TileSource {
	const char* id;             // Names its folder in the cache.
	const char* name;           // What the user sees on the button.
	const char* url_template;   // {z}, {x} and {y} are replaced by the tile's numbers.
	int         max_zoom;       // The deepest level shown; past that the pictures are enlarged.
	int         cached_max_zoom;   // The deepest level the server is known to have ready-made tiles of.
	const char* deep_url_template; // Where to ask for a picture of the place of a tile deeper than that, drawn when asked for
	                               // (null if there is none): {xmin}, {ymin}, {xmax} and {ymax} are replaced by the
	                               // tile's edges in Web Mercator meters. Tried only after the ready-made ones turn out not to be there.
	int         max_out;        // The most requests to it out at once, however slow they are (see requests_allowed()).
	const char* credit;         // The line the terms of use ask to be shown.
};

namespace map_tiles {

enum Style { STYLE_STREET = 0, STYLE_SATELLITE = 1, STYLE_COUNT = 2 };

const TileSource& source(int style);

// How many requests to a source are out at once: this many when they are being answered promptly...
const int BASE_REQUESTS_OUT = 2;
// ... and one more for each of them that has been out longer than this many seconds, so a few slow
// answers (some servers now and then take ten seconds to draw a picture) don't hold up all the rest.
// Never more than the source's `max_out`: a source that asks for only two at a time has a `max_out` of two.
const double STALL_SECONDS = 1.5;
int requests_allowed(const TileSource& source, int stalled);

struct TileKey {
	int z;
	int x;
	int y;
	TileKey() : z(0), x(0), y(0) {}
	TileKey(int level, int column, int row) : z(level), x(column), y(row) {}
	bool operator==(const TileKey& other) const { return z == other.z && x == other.x && y == other.y; }
	bool operator!=(const TileKey& other) const { return !(*this == other); }
	bool operator<(const TileKey& other) const {
		if (z != other.z) return z < other.z;
		if (x != other.x) return x < other.x;
		return y < other.y;
	}
};

// The tile level to use for a view at the (fractional) zoom: the nearest whole level, so a tile is
// drawn between 0.7 and 1.4 times its own size, held between 0 and `max_zoom`.
int tile_zoom_for(double zoom, int max_zoom);

// The tiles of level `z` that show in the pane, nearest the middle of it first. At most `max_tiles`.
std::vector<TileKey> visible_tiles(const MapViewport& view, int z, int max_tiles);

// Where the tile lands in the pane, in pixels from its top left corner.
void tile_rect(const MapViewport& view, const TileKey& tile, float* left, float* top, float* right, float* bottom);

// The part of a picture that stands in for a tile: the tile `levels_up` levels above it (its parent is
// 1) that covers it, and the corners of the tile's area in that picture, as fractions 0..1 of its width and height.
struct StandIn {
	TileKey ancestor;
	float   u0;
	float   v0;
	float   u1;
	float   v1;
};
StandIn ancestor_of(const TileKey& tile, int levels_up);

// The address to ask for a tile at: the template with {z}, {x}, {y} and the edges ({xmin} and so on, see
// TileSource::deep_url_template) replaced.
std::string expand_url(const char* url_template, const TileKey& tile);

// The parts of a web address the fetching needs.
struct UrlParts {
	bool        secure;
	std::string host;
	int         port;
	std::string path;   // From the first slash, with the query if there is one.
	UrlParts() : secure(false), host(), port(0), path() {}
};
bool split_url(const std::string& url, UrlParts* parts);

// Where a tile's picture is kept, under the cache's folder: "<source id>/<z>/<x>/<y>.png", with
// "/" between the parts (the file helpers take that on Windows too). The extra data kept with it
// (see TileMeta) has the same name ending ".meta".
std::string cache_path(const char* source_id, const TileKey& tile);
std::string meta_path(const char* source_id, const TileKey& tile);

// What is known about a tile on the disk besides its picture.
struct TileMeta {
	std::string etag;       // The server's name for that version of the picture (empty if it gave none).
	int64_t     fetched;    // When it was asked for or last found to be current: seconds since 1970.
	TileMeta() : etag(), fetched(0) {}
};
std::string serialize_meta(const TileMeta& meta);
// False if the text isn't a TileMeta (a tile with unreadable data is fetched again).
bool parse_meta(const std::string& text, TileMeta* meta);

// How long a picture on the disk is used without asking the server whether it has changed.
// OpenStreetMap asks that tiles be kept at least 7 days; roads change slowly and a flight is
// usually edited long after the fact, so the server is only asked after much longer than that.
const int64_t SECONDS_PER_DAY = 86400;
const int64_t AUTOMATIC_REFRESH_AGE = 180 * SECONDS_PER_DAY;
// What the user's "Refresh Map" asks about: tiles this old.
const int64_t MANUAL_REFRESH_AGE = 7 * SECONDS_PER_DAY;

bool refresh_due(const TileMeta& meta, int64_t now, int64_t age_limit);

// The text of the value of a header ("ETag") in the raw headers of a reply, lines separated by CR LF
// or LF. Empty if it isn't there. Names match whatever the case.
std::string find_header(const std::string& raw_headers, const char* name);

// A file in the cache (a picture), for choosing which to delete when it grows too big.
struct CacheFile {
	int64_t bytes;      // The picture and its data file together.
	int64_t modified;   // Bigger is later.
};
// Which files to delete so the rest is at most `limit_bytes`: the oldest, as many as it takes.
// The result is indexes into `files`, oldest first.
std::vector<size_t> choose_evictions(const std::vector<CacheFile>& files, int64_t limit_bytes);

// Getting a head start: where the flight begins on the ground, and the few tiles to fetch before the
// user has asked to see the map.

// What is needed of each sample of the telemetry to find where the flight starts.
struct TrackSample {
	double latitude;
	double longitude;
	float  speed_kts;
	bool   has_fix;
	TrackSample() : latitude(0.0), longitude(0.0), speed_kts(0.0f), has_fix(false) {}
};

// The first place the telemetry has a position, and the first place after it that is faster than
// walking (where the pilot starts running for the launch).
struct LaunchArea {
	MapPoint first;
	MapPoint brisk;
	double   latitude_degrees;   // At the first fix.
	LaunchArea() : first(), brisk(), latitude_degrees(0.0) {}
};

// False if there is no position in the telemetry at all. If it never goes faster than `brisk_kts`, `brisk` is `first`.
bool find_launch_area(const std::vector<TrackSample>& samples, float brisk_kts, LaunchArea* area);

// Meters on the ground to one unit of the world coordinates, at that latitude.
double meters_per_world_unit(double latitude_degrees);

// The tiles to fetch for the launch area: first the ones of level `detail_zoom` that cover the area from `first`
// to `brisk` with `pad_meters` added all around (nearest `brisk` first, at most `detail_max`), then, while there
// is room under `total_max`, the one tile at each level above that (down to level 14) that has `brisk` in it.
std::vector<TileKey> launch_tiles(const LaunchArea& area, int detail_zoom, double pad_meters, int detail_max, int total_max);

}  // namespace map_tiles
