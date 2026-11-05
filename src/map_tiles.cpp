#include "map_tiles.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace map_tiles {

namespace {

// OpenStreetMap's tiles: https://operations.osmfoundation.org/policies/tiles/ (HTTPS, a User-Agent that
// says who is asking, caches respected, a credit on screen, no bulk downloading).
//
// The USGS National Map's imagery: free to use, the United States only. Its tiles are numbered
// level/row/column, the other way round from OpenStreetMap's, and it is believed to have ready-made
// ones down to level 16 only (about 2 m a pixel; the aerial photographs are 0.3 to 1 m). Below that
// the NAIP imagery service can draw a picture of the place of any tile when asked.
// (OpenStreetMap's tile usage policy asks for no more than two download threads, so it is held to two.)
const TileSource SOURCES[STYLE_COUNT] = {
	{ "osm",  "Street",    "https://tile.openstreetmap.org/{z}/{x}/{y}.png", 19, 19, nullptr, 2,
	  "\xC2\xA9 OpenStreetMap contributors" },
	{ "usgs", "Satellite", "https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/{z}/{y}/{x}", 19, 16,
	  "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/ImageServer/exportImage?bbox={xmin},{ymin},{xmax},{ymax}&bboxSR=3857&imageSR=3857&size=256,256&format=jpg&f=image", 6,
	  "Map services and data available from U.S. Geological Survey, National Geospatial Program" },
};

std::string replace_all(const std::string& text, const char* what, const std::string& with) {
	std::string result;
	const size_t length = strlen(what);
	size_t at = 0;
	while (true) {
		const size_t found = text.find(what, at);
		if (found == std::string::npos) {
			result += text.substr(at);
			return result;
		}
		result += text.substr(at, found - at);
		result += with;
		at = found + length;
	}
}

std::string number(long long value) {
	char text[32];
	snprintf(text, sizeof(text), "%lld", value);
	return std::string(text);
}

bool same_letter(char a, char b) {
	if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
	if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
	return a == b;
}

}  // namespace

int requests_allowed(const TileSource& source, int stalled) {
	int allowed = BASE_REQUESTS_OUT + (stalled > 0 ? stalled : 0);
	if (allowed > source.max_out)
		allowed = source.max_out;
	return allowed;
}

const TileSource& source(int style) {
	if (style < 0 || style >= STYLE_COUNT)
		style = STYLE_STREET;
	return SOURCES[style];
}

int tile_zoom_for(double zoom, int max_zoom) {
	int level = (int)floor(zoom + 0.5);
	if (level > max_zoom)
		level = max_zoom;
	if (level < 0)
		level = 0;
	return level;
}

void tile_rect(const MapViewport& view, const TileKey& tile, float* left, float* top, float* right, float* bottom) {
	const double n = (double)(1 << tile.z);
	const ScreenPoint a = view.to_screen(MapPoint((double)tile.x / n, (double)tile.y / n));
	const ScreenPoint b = view.to_screen(MapPoint((double)(tile.x + 1) / n, (double)(tile.y + 1) / n));
	*left = a.x;
	*top = a.y;
	*right = b.x;
	*bottom = b.y;
}

std::vector<TileKey> visible_tiles(const MapViewport& view, int z, int max_tiles) {
	std::vector<TileKey> tiles;
	if (view.width() < 1.0f || view.height() < 1.0f || max_tiles < 1)
		return tiles;
	const int n = 1 << z;
	const MapPoint top_left = view.to_world(0.0f, 0.0f);
	const MapPoint bottom_right = view.to_world(view.width(), view.height());
	int x0 = (int)floor(top_left.x * (double)n);
	int y0 = (int)floor(top_left.y * (double)n);
	int x1 = (int)floor(bottom_right.x * (double)n);
	int y1 = (int)floor(bottom_right.y * (double)n);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > n - 1) x1 = n - 1;
	if (y1 > n - 1) y1 = n - 1;
	if (x1 < x0 || y1 < y0)
		return tiles;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++)
			tiles.push_back(TileKey(z, x, y));
	}
	// Nearest the middle first: those are what the user is looking at, so they are asked for first.
	const double middle_x = view.center().x * (double)n;
	const double middle_y = view.center().y * (double)n;
	std::vector<std::pair<double, size_t> > order;
	for (size_t i = 0; i < tiles.size(); i++) {
		const double dx = (double)tiles[i].x + 0.5 - middle_x;
		const double dy = (double)tiles[i].y + 0.5 - middle_y;
		order.push_back(std::make_pair(dx * dx + dy * dy, i));
	}
	std::sort(order.begin(), order.end());
	std::vector<TileKey> sorted;
	for (size_t i = 0; i < order.size() && (int)sorted.size() < max_tiles; i++)
		sorted.push_back(tiles[order[i].second]);
	return sorted;
}

StandIn ancestor_of(const TileKey& tile, int levels_up) {
	if (levels_up > tile.z)
		levels_up = tile.z;
	if (levels_up < 0)
		levels_up = 0;
	StandIn result;
	result.ancestor = TileKey(tile.z - levels_up, tile.x >> levels_up, tile.y >> levels_up);
	const float size = 1.0f / (float)(1 << levels_up);
	const int within_x = tile.x - (result.ancestor.x << levels_up);
	const int within_y = tile.y - (result.ancestor.y << levels_up);
	result.u0 = (float)within_x * size;
	result.v0 = (float)within_y * size;
	result.u1 = result.u0 + size;
	result.v1 = result.v0 + size;
	return result;
}

std::string expand_url(const char* url_template, const TileKey& tile) {
	std::string url = replace_all(url_template, "{z}", number(tile.z));
	url = replace_all(url, "{x}", number(tile.x));
	url = replace_all(url, "{y}", number(tile.y));
	if (url.find("{xmin}") != std::string::npos) {
		// The tile's edges in meters on the Web Mercator map: the world is this many meters around, with 0, 0 in the middle.
		const double WORLD_METERS = 40075016.68557849;
		const double size = WORLD_METERS / (double)(1 << tile.z);
		const double xmin = -WORLD_METERS / 2.0 + (double)tile.x * size;
		const double ymax = WORLD_METERS / 2.0 - (double)tile.y * size;
		char text[48];
		snprintf(text, sizeof(text), "%.3f", xmin);
		url = replace_all(url, "{xmin}", text);
		snprintf(text, sizeof(text), "%.3f", xmin + size);
		url = replace_all(url, "{xmax}", text);
		snprintf(text, sizeof(text), "%.3f", ymax - size);
		url = replace_all(url, "{ymin}", text);
		snprintf(text, sizeof(text), "%.3f", ymax);
		url = replace_all(url, "{ymax}", text);
	}
	return url;
}

bool split_url(const std::string& url, UrlParts* parts) {
	size_t host_start;
	if (url.compare(0, 8, "https://") == 0) {
		parts->secure = true;
		parts->port = 443;
		host_start = 8;
	} else if (url.compare(0, 7, "http://") == 0) {
		parts->secure = false;
		parts->port = 80;
		host_start = 7;
	} else {
		return false;
	}
	size_t path_start = url.find('/', host_start);
	std::string authority = url.substr(host_start, path_start == std::string::npos ? std::string::npos : path_start - host_start);
	parts->path = path_start == std::string::npos ? std::string("/") : url.substr(path_start);
	const size_t colon = authority.find(':');
	if (colon != std::string::npos) {
		const int port = atoi(authority.c_str() + colon + 1);
		if (port < 1 || port > 65535)
			return false;
		parts->port = port;
		authority = authority.substr(0, colon);
	}
	if (authority.empty())
		return false;
	parts->host = authority;
	return true;
}

std::string cache_path(const char* source_id, const TileKey& tile) {
	return std::string(source_id) + "/" + number(tile.z) + "/" + number(tile.x) + "/" + number(tile.y) + ".png";
}

std::string meta_path(const char* source_id, const TileKey& tile) {
	return std::string(source_id) + "/" + number(tile.z) + "/" + number(tile.x) + "/" + number(tile.y) + ".meta";
}

std::string serialize_meta(const TileMeta& meta) {
	std::string text = "fetched:" + number(meta.fetched) + "\n";
	if (!meta.etag.empty())
		text += "etag:" + meta.etag + "\n";
	return text;
}

bool parse_meta(const std::string& text, TileMeta* meta) {
	TileMeta result;
	bool have_time = false;
	size_t at = 0;
	while (at < text.size()) {
		size_t end = text.find('\n', at);
		if (end == std::string::npos)
			end = text.size();
		std::string line = text.substr(at, end - at);
		while (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		at = end + 1;
		const size_t colon = line.find(':');
		if (colon == std::string::npos)
			continue;
		const std::string key = line.substr(0, colon);
		const std::string value = line.substr(colon + 1);
		if (key == "fetched") {
			char* stop = NULL;
			const long long seconds = strtoll(value.c_str(), &stop, 10);
			if (stop == value.c_str() || seconds <= 0)
				return false;
			result.fetched = (int64_t)seconds;
			have_time = true;
		} else if (key == "etag") {
			result.etag = value;
		}
	}
	if (!have_time)
		return false;
	*meta = result;
	return true;
}

bool refresh_due(const TileMeta& meta, int64_t now, int64_t age_limit) {
	return now - meta.fetched > age_limit;
}

std::string find_header(const std::string& raw_headers, const char* name) {
	const size_t name_length = strlen(name);
	size_t at = 0;
	while (at < raw_headers.size()) {
		size_t end = raw_headers.find('\n', at);
		if (end == std::string::npos)
			end = raw_headers.size();
		std::string line = raw_headers.substr(at, end - at);
		at = end + 1;
		while (!line.empty() && line[line.size() - 1] == '\r')
			line.erase(line.size() - 1);
		if (line.size() <= name_length || line[name_length] != ':')
			continue;
		bool match = true;
		for (size_t i = 0; i < name_length && match; i++)
			match = same_letter(line[i], name[i]);
		if (!match)
			continue;
		size_t start = name_length + 1;
		while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
			start++;
		return line.substr(start);
	}
	return std::string();
}

std::vector<size_t> choose_evictions(const std::vector<CacheFile>& files, int64_t limit_bytes) {
	std::vector<size_t> evictions;
	int64_t total = 0;
	for (size_t i = 0; i < files.size(); i++)
		total += files[i].bytes;
	if (total <= limit_bytes)
		return evictions;
	std::vector<std::pair<int64_t, size_t> > by_age;
	for (size_t i = 0; i < files.size(); i++)
		by_age.push_back(std::make_pair(files[i].modified, i));
	std::sort(by_age.begin(), by_age.end());
	for (size_t i = 0; i < by_age.size() && total > limit_bytes; i++) {
		evictions.push_back(by_age[i].second);
		total -= files[by_age[i].second].bytes;
	}
	return evictions;
}

bool find_launch_area(const std::vector<TrackSample>& samples, float brisk_kts, LaunchArea* area) {
	size_t first = 0;
	while (first < samples.size() && !samples[first].has_fix)
		first++;
	if (first >= samples.size())
		return false;
	area->first = map_projection::from_lat_lon(samples[first].latitude, samples[first].longitude);
	area->latitude_degrees = samples[first].latitude;
	area->brisk = area->first;
	for (size_t i = first; i < samples.size(); i++) {
		if (samples[i].has_fix && samples[i].speed_kts > brisk_kts) {
			area->brisk = map_projection::from_lat_lon(samples[i].latitude, samples[i].longitude);
			break;
		}
	}
	return true;
}

double meters_per_world_unit(double latitude_degrees) {
	const double EQUATOR_METERS = 40075016.686;
	return EQUATOR_METERS * cos(latitude_degrees * 3.14159265358979 / 180.0);
}

std::vector<TileKey> launch_tiles(const LaunchArea& area, int detail_zoom, double pad_meters, int detail_max, int total_max) {
	std::vector<TileKey> tiles;
	if (detail_max < 1 || total_max < 1)
		return tiles;
	const double n = (double)(1 << detail_zoom);
	const double pad = pad_meters / meters_per_world_unit(area.latitude_degrees);
	const double low_x = std::min(area.first.x, area.brisk.x) - pad;
	const double high_x = std::max(area.first.x, area.brisk.x) + pad;
	const double low_y = std::min(area.first.y, area.brisk.y) - pad;
	const double high_y = std::max(area.first.y, area.brisk.y) + pad;
	int x0 = (int)floor(low_x * n);
	int x1 = (int)floor(high_x * n);
	int y0 = (int)floor(low_y * n);
	int y1 = (int)floor(high_y * n);
	const int last = (1 << detail_zoom) - 1;
	x0 = std::max(x0, 0);
	y0 = std::max(y0, 0);
	x1 = std::min(x1, last);
	y1 = std::min(y1, last);
	std::vector<std::pair<double, TileKey> > by_distance;
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			const double dx = (double)x + 0.5 - area.brisk.x * n;
			const double dy = (double)y + 0.5 - area.brisk.y * n;
			by_distance.push_back(std::make_pair(dx * dx + dy * dy, TileKey(detail_zoom, x, y)));
		}
	}
	std::sort(by_distance.begin(), by_distance.end());
	for (size_t i = 0; i < by_distance.size() && (int)tiles.size() < detail_max && (int)tiles.size() < total_max; i++)
		tiles.push_back(by_distance[i].second);
	// The wider pictures, for when the map is zoomed out from there.
	for (int z = detail_zoom - 1; z >= 14 && (int)tiles.size() < total_max; z--) {
		const int size = 1 << z;
		int x = (int)floor(area.brisk.x * (double)size);
		int y = (int)floor(area.brisk.y * (double)size);
		x = std::max(0, std::min(size - 1, x));
		y = std::max(0, std::min(size - 1, y));
		tiles.push_back(TileKey(z, x, y));
	}
	return tiles;
}

}  // namespace map_tiles
