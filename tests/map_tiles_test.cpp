#include <math.h>
#include <stdio.h>

#include "map_tiles.h"

using namespace map_tiles;

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va = (a), vb = (b); if (fabs(va - vb) > (tol)) { printf("FAIL line %d: %s = %.9f, wanted %.9f\n", __LINE__, #a, va, vb); g_failures++; } } while (0)
#define CHECK_TEXT(a, b) do { std::string va = (a), vb = (b); if (va != vb) { printf("FAIL line %d: %s = \"%s\", wanted \"%s\"\n", __LINE__, #a, va.c_str(), vb.c_str()); g_failures++; } } while (0)

static void test_tile_zoom() {
	CHECK(tile_zoom_for(14.4, 19) == 14);
	CHECK(tile_zoom_for(14.5, 19) == 15);
	CHECK(tile_zoom_for(14.6, 19) == 15);
	CHECK(tile_zoom_for(18.9, 16) == 16);   // Past the server's deepest level its tiles are enlarged.
	CHECK(tile_zoom_for(2.0, 19) == 2);
	CHECK(tile_zoom_for(-1.0, 19) == 0);
}

static void test_visible_tiles() {
	// At zoom 2 a 1024 pixel pane centered on the world shows exactly the 16 tiles of level 2, once each.
	MapViewport world;
	world.set_size(1024.0f, 1024.0f);
	world.set_view(MapPoint(0.5, 0.5), 2.0);
	std::vector<TileKey> all = visible_tiles(world, 2, 100);
	CHECK(all.size() == 16);
	for (size_t i = 0; i < all.size(); i++) {
		for (size_t j = i + 1; j < all.size(); j++)
			CHECK(all[i] != all[j]);
		CHECK(all[i].x >= 0 && all[i].x < 4 && all[i].y >= 0 && all[i].y < 4);
	}
	// Ones in the middle come first.
	CHECK((all[0].x == 1 || all[0].x == 2) && (all[0].y == 1 || all[0].y == 2));
	CHECK(visible_tiles(world, 2, 5).size() == 5);
	CHECK(visible_tiles(world, 2, 0).empty());

	// A pane bigger than the world shows only the tiles that exist.
	MapViewport wide;
	wide.set_size(4000.0f, 3000.0f);
	wide.set_view(MapPoint(0.5, 0.5), 2.0);
	CHECK(visible_tiles(wide, 2, 100).size() == 16);

	// A zoomed in pane: 800 x 600 at level 15 is a handful of tiles, and each lies over the pane.
	MapViewport view;
	view.set_size(800.0f, 600.0f);
	view.set_view(map_projection::from_lat_lon(37.7167, -122.5025), 15.0);
	std::vector<TileKey> tiles = visible_tiles(view, 15, 100);
	CHECK(tiles.size() >= 12 && tiles.size() <= 20);
	for (size_t i = 0; i < tiles.size(); i++) {
		float l, t, r, b;
		tile_rect(view, tiles[i], &l, &t, &r, &b);
		CHECK(r > 0.0f && l < 800.0f && b > 0.0f && t < 600.0f);
		CHECK_NEAR(r - l, 256.0, 0.05);
		CHECK_NEAR(b - t, 256.0, 0.05);
	}
	// The center of the pane is inside the first tile.
	float l, t, r, b;
	tile_rect(view, tiles[0], &l, &t, &r, &b);
	CHECK(l <= 400.0f && r >= 400.0f && t <= 300.0f && b >= 300.0f);

	// Fractional zoom: tiles of level 15 at zoom 15.5 are 362 pixels.
	view.set_view(view.center(), 15.5);
	tile_rect(view, tiles[0], &l, &t, &r, &b);
	CHECK_NEAR(r - l, 256.0 * pow(2.0, 0.5), 0.1);

	// Neighbors share an edge exactly.
	float l2, t2, r2, b2;
	tile_rect(view, TileKey(15, tiles[0].x + 1, tiles[0].y), &l2, &t2, &r2, &b2);
	CHECK_NEAR(l2, r, 1e-3);
	CHECK_NEAR(t2, t, 1e-3);
}

static void test_ancestors() {
	StandIn parent = ancestor_of(TileKey(10, 5, 7), 1);
	CHECK(parent.ancestor == TileKey(9, 2, 3));
	CHECK_NEAR(parent.u0, 0.5, 1e-6);   // x is odd: the right half...
	CHECK_NEAR(parent.u1, 1.0, 1e-6);
	CHECK_NEAR(parent.v0, 0.5, 1e-6);   // ... and y is odd: the lower half.
	CHECK_NEAR(parent.v1, 1.0, 1e-6);
	StandIn even = ancestor_of(TileKey(10, 4, 6), 1);
	CHECK(even.ancestor == TileKey(9, 2, 3));
	CHECK_NEAR(even.u0, 0.0, 1e-6);
	CHECK_NEAR(even.u1, 0.5, 1e-6);
	CHECK_NEAR(even.v0, 0.0, 1e-6);
	CHECK_NEAR(even.v1, 0.5, 1e-6);
	StandIn far = ancestor_of(TileKey(10, 13, 6), 3);
	CHECK(far.ancestor == TileKey(7, 1, 0));
	CHECK_NEAR(far.u0, 5.0 / 8.0, 1e-6);
	CHECK_NEAR(far.u1, 6.0 / 8.0, 1e-6);
	CHECK_NEAR(far.v0, 6.0 / 8.0, 1e-6);
	CHECK_NEAR(far.v1, 7.0 / 8.0, 1e-6);
	// Nothing is above level 0; the whole picture is the answer.
	StandIn top = ancestor_of(TileKey(1, 1, 1), 5);
	CHECK(top.ancestor == TileKey(0, 0, 0));
	CHECK_NEAR(top.u0, 0.5, 1e-6);
	CHECK_NEAR(top.u1, 1.0, 1e-6);
	StandIn same = ancestor_of(TileKey(3, 2, 1), 0);
	CHECK(same.ancestor == TileKey(3, 2, 1));
	CHECK_NEAR(same.u0, 0.0, 1e-6);
	CHECK_NEAR(same.u1, 1.0, 1e-6);
}

static void test_urls() {
	CHECK_TEXT(expand_url(source(STYLE_STREET).url_template, TileKey(15, 5233, 12663)), "https://tile.openstreetmap.org/15/5233/12663.png");
	// The USGS server numbers its tiles level/row/column.
	CHECK_TEXT(expand_url(source(STYLE_SATELLITE).url_template, TileKey(15, 5233, 12663)),
		"https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/tile/15/12663/5233");
	CHECK(source(STYLE_STREET).max_zoom == 19);
	// Past the ready-made tiles the satellite pictures are drawn on request, by edges in meters.
	CHECK(source(STYLE_SATELLITE).cached_max_zoom == 16);
	CHECK(source(STYLE_SATELLITE).max_zoom == 19);
	CHECK(source(STYLE_SATELLITE).deep_url_template != NULL);
	CHECK(source(STYLE_STREET).deep_url_template == NULL);
	// Level 1, tile (1, 0): the east half's top (northeast quarter) is 0 to 20037508.343 across and up.
	CHECK_TEXT(expand_url("x?bbox={xmin},{ymin},{xmax},{ymax}", TileKey(1, 1, 0)), "x?bbox=0.000,0.000,20037508.343,20037508.343");
	CHECK_TEXT(expand_url("x?bbox={xmin},{ymin},{xmax},{ymax}", TileKey(1, 0, 1)), "x?bbox=-20037508.343,-20037508.343,0.000,0.000");
	// A level 17 tile is 305.748 m across; its edges are that far apart, with the right corner (the origin is the northwest).
	{
		std::string url = expand_url("{xmin} {ymin} {xmax} {ymax}", TileKey(17, 50654, 20912));
		double xmin, ymin, xmax, ymax;
		CHECK(sscanf(url.c_str(), "%lf %lf %lf %lf", &xmin, &ymin, &xmax, &ymax) == 4);
		CHECK_NEAR(xmax - xmin, 305.748, 0.01);
		CHECK_NEAR(ymax - ymin, 305.748, 0.01);
		CHECK_NEAR(xmin, -20037508.343 + 50654.0 * 305.7481131, 0.1);
		CHECK_NEAR(ymax, 20037508.343 - 20912.0 * 305.7481131, 0.1);
	}
	CHECK(source(99).max_zoom == source(STYLE_STREET).max_zoom);

	UrlParts parts;
	CHECK(split_url("https://tile.openstreetmap.org/15/5233/12663.png", &parts));
	CHECK(parts.secure && parts.port == 443);
	CHECK_TEXT(parts.host, "tile.openstreetmap.org");
	CHECK_TEXT(parts.path, "/15/5233/12663.png");
	CHECK(split_url("http://localhost:8080/a/b?c=d", &parts));
	CHECK(!parts.secure && parts.port == 8080);
	CHECK_TEXT(parts.host, "localhost");
	CHECK_TEXT(parts.path, "/a/b?c=d");
	CHECK(split_url("https://example.com", &parts));
	CHECK_TEXT(parts.path, "/");
	CHECK(!split_url("ftp://example.com/x", &parts));
	CHECK(!split_url("https:///x", &parts));
	CHECK(!split_url("https://host:99999/x", &parts));
}

static void test_cache_names() {
	CHECK_TEXT(cache_path("osm", TileKey(15, 5233, 12663)), "osm/15/5233/12663.png");
	CHECK_TEXT(meta_path("osm", TileKey(15, 5233, 12663)), "osm/15/5233/12663.meta");
	CHECK_TEXT(cache_path("usgs", TileKey(0, 0, 0)), "usgs/0/0/0.png");
}

static void test_meta() {
	TileMeta meta;
	meta.etag = "\"abc123\"";
	meta.fetched = 1700000000;
	TileMeta back;
	CHECK(parse_meta(serialize_meta(meta), &back));
	CHECK_TEXT(back.etag, "\"abc123\"");
	CHECK(back.fetched == 1700000000);
	// No ETag is fine.
	TileMeta plain;
	plain.fetched = 1234567;
	CHECK(parse_meta(serialize_meta(plain), &back));
	CHECK(back.etag.empty());
	CHECK(back.fetched == 1234567);
	// An ETag with a colon in it (W/"a:b") comes back whole; CR LF line ends are fine.
	CHECK(parse_meta("fetched:99\r\netag:W/\"a:b\"\r\n", &back));
	CHECK_TEXT(back.etag, "W/\"a:b\"");
	CHECK(back.fetched == 99);
	// Not a TileMeta.
	CHECK(!parse_meta("", &back));
	CHECK(!parse_meta("etag:x\n", &back));
	CHECK(!parse_meta("fetched:abc\n", &back));
	CHECK(!parse_meta("fetched:0\n", &back));
}

static void test_refresh_rules() {
	TileMeta meta;
	meta.fetched = 1000000000;
	const int64_t day = SECONDS_PER_DAY;
	// A tile is used as it is for months before the server is asked about it on its own...
	CHECK(!refresh_due(meta, meta.fetched + 100 * day, AUTOMATIC_REFRESH_AGE));
	CHECK(!refresh_due(meta, meta.fetched + 180 * day, AUTOMATIC_REFRESH_AGE));
	CHECK(refresh_due(meta, meta.fetched + 181 * day, AUTOMATIC_REFRESH_AGE));
	// ... and the user's Refresh button asks about anything older than a week.
	CHECK(!refresh_due(meta, meta.fetched + 6 * day, MANUAL_REFRESH_AGE));
	CHECK(!refresh_due(meta, meta.fetched + 7 * day, MANUAL_REFRESH_AGE));
	CHECK(refresh_due(meta, meta.fetched + 8 * day, MANUAL_REFRESH_AGE));
	CHECK(MANUAL_REFRESH_AGE < AUTOMATIC_REFRESH_AGE);
	CHECK(MANUAL_REFRESH_AGE >= 7 * day);
}

static void test_headers() {
	const std::string headers =
		"HTTP/1.1 200 OK\r\n"
		"Server: nginx\r\n"
		"Cache-Control: max-age=28800\r\n"
		"ETag: \"5f3a9c\"\r\n"
		"Content-Type: image/png\r\n";
	CHECK_TEXT(find_header(headers, "ETag"), "\"5f3a9c\"");
	CHECK_TEXT(find_header(headers, "etag"), "\"5f3a9c\"");
	CHECK_TEXT(find_header(headers, "Content-Type"), "image/png");
	CHECK_TEXT(find_header(headers, "Cache-Control"), "max-age=28800");
	CHECK_TEXT(find_header(headers, "Content"), "");   // A longer name that starts the same is not it.
	CHECK_TEXT(find_header(headers, "Last-Modified"), "");
	CHECK_TEXT(find_header("etag:   spaced  \nother: x", "ETag"), "spaced  ");
	CHECK_TEXT(find_header("", "ETag"), "");
}

static void test_evictions() {
	std::vector<CacheFile> files;
	const int64_t times[5] = {50, 10, 40, 20, 30};
	for (int i = 0; i < 5; i++) {
		CacheFile file;
		file.bytes = 100;
		file.modified = times[i];
		files.push_back(file);
	}
	// Under the limit: nothing goes.
	CHECK(choose_evictions(files, 500).empty());
	CHECK(choose_evictions(files, 1000).empty());
	// 500 bytes held to 300: the two oldest (indexes 1 and 3).
	std::vector<size_t> out = choose_evictions(files, 300);
	CHECK(out.size() == 2);
	CHECK(out.size() == 2 && out[0] == 1 && out[1] == 3);
	// Held to 0: everything, oldest first.
	out = choose_evictions(files, 0);
	CHECK(out.size() == 5);
	CHECK(out.size() == 5 && out[0] == 1 && out[4] == 0);
	// A limit that isn't a multiple of the sizes still ends up under.
	out = choose_evictions(files, 250);
	CHECK(out.size() == 3);
	CHECK(choose_evictions(std::vector<CacheFile>(), 0).empty());
}

static TrackSample sample(double lat, double lon, float speed, bool fix) {
	TrackSample s;
	s.latitude = lat;
	s.longitude = lon;
	s.speed_kts = speed;
	s.has_fix = fix;
	return s;
}

static void test_launch_area() {
	std::vector<TrackSample> samples;
	LaunchArea area;
	CHECK(!find_launch_area(samples, 6.0f, &area));
	// No fix yet at first, then standing, a slow walk, a brisk one.
	samples.push_back(sample(0.0, 0.0, 0.0f, false));
	CHECK(!find_launch_area(samples, 6.0f, &area));
	samples.push_back(sample(37.7160, -122.5000, 0.0f, true));
	samples.push_back(sample(37.7160, -122.5000, 2.0f, true));
	samples.push_back(sample(37.71601, -122.50003, 5.9f, true));
	samples.push_back(sample(37.71604, -122.50010, 6.1f, true));
	samples.push_back(sample(37.7170, -122.5100, 30.0f, true));
	CHECK(find_launch_area(samples, 6.0f, &area));
	CHECK_NEAR(area.latitude_degrees, 37.7160, 1e-9);
	MapPoint first = map_projection::from_lat_lon(37.7160, -122.5000);
	MapPoint brisk = map_projection::from_lat_lon(37.71604, -122.50010);
	CHECK_NEAR(area.first.x, first.x, 1e-12);
	CHECK_NEAR(area.brisk.x, brisk.x, 1e-12);
	CHECK_NEAR(area.brisk.y, brisk.y, 1e-12);
	// A fix-less sample that is fast is ignored.
	samples[3] = sample(0.0, 0.0, 50.0f, false);
	CHECK(find_launch_area(samples, 6.0f, &area));
	CHECK_NEAR(area.brisk.x, brisk.x, 1e-12);
	// Never brisk: the brisk place is where it starts.
	std::vector<TrackSample> slow;
	slow.push_back(sample(10.0, 20.0, 1.0f, true));
	slow.push_back(sample(10.0001, 20.0001, 2.0f, true));
	CHECK(find_launch_area(slow, 6.0f, &area));
	CHECK_NEAR(area.brisk.x, area.first.x, 1e-15);
	CHECK_NEAR(area.brisk.y, area.first.y, 1e-15);
}

static void test_launch_tiles() {
	// Equator: a meter is 1/40075016.686 of the world; at 60 degrees twice that.
	CHECK_NEAR(meters_per_world_unit(0.0), 40075016.686, 1.0);
	CHECK_NEAR(meters_per_world_unit(60.0), 40075016.686 / 2.0, 1.0);

	// Walking about 45 m at Fort Funston: the level 18 tiles (120 m each) around it are a few, nearest brisk first.
	LaunchArea area;
	area.latitude_degrees = 37.7160;
	area.first = map_projection::from_lat_lon(37.7160, -122.5000);
	area.brisk = map_projection::from_lat_lon(37.71620, -122.50045);
	std::vector<TileKey> tiles = launch_tiles(area, 18, 60.0, 6, 10);
	CHECK(tiles.size() <= 10);
	int detail = 0;
	for (size_t i = 0; i < tiles.size(); i++) {
		if (tiles[i].z == 18)
			detail++;
	}
	CHECK(detail >= 1 && detail <= 6);
	// The first tile has the brisk point in it.
	const double n18 = (double)(1 << 18);
	CHECK(tiles[0] == TileKey(18, (int)floor(area.brisk.x * n18), (int)floor(area.brisk.y * n18)));
	// Detail tiles come first, then 17, 16, 15, 14 each once, each holding the brisk point.
	size_t at = (size_t)detail;
	for (int z = 17; z >= 14 && at < tiles.size(); z--, at++) {
		CHECK(tiles[at].z == z);
		const double n = (double)(1 << z);
		CHECK(tiles[at] == TileKey(z, (int)floor(area.brisk.x * n), (int)floor(area.brisk.y * n)));
	}
	CHECK(tiles.size() == (size_t)detail + 4);
	// No tile twice.
	for (size_t i = 0; i < tiles.size(); i++) {
		for (size_t j = i + 1; j < tiles.size(); j++)
			CHECK(tiles[i] != tiles[j]);
	}
	// Every detail tile is within the padded box (one tile of slop each way).
	const double pad = 60.0 / meters_per_world_unit(area.latitude_degrees);
	for (int i = 0; i < detail; i++) {
		const double left = (double)tiles[i].x / n18;
		const double right = (double)(tiles[i].x + 1) / n18;
		CHECK(right >= area.first.x - pad - 1e-12 && left <= area.brisk.x + pad + 1e-12);
	}

	// A wide area is held to the detail limit, and the total limit.
	LaunchArea wide = area;
	wide.brisk = map_projection::from_lat_lon(37.7260, -122.5200);
	tiles = launch_tiles(wide, 18, 60.0, 6, 10);
	CHECK(tiles.size() == 10);
	int wide_detail = 0;
	for (size_t i = 0; i < tiles.size(); i++) {
		if (tiles[i].z == 18)
			wide_detail++;
	}
	CHECK(wide_detail == 6);
	tiles = launch_tiles(wide, 18, 60.0, 6, 3);
	CHECK(tiles.size() == 3);
	CHECK(launch_tiles(wide, 18, 60.0, 0, 10).empty());
	// Standing still: just the tiles round the spot.
	LaunchArea still = area;
	still.brisk = still.first;
	tiles = launch_tiles(still, 18, 60.0, 6, 10);
	CHECK(!tiles.empty() && tiles.size() <= 4 + 4);
}

static void test_requests_allowed() {
	// OpenStreetMap is held to two however slow it is; the USGS goes up one for each slow request, to six.
	for (int stalled = 0; stalled <= 8; stalled++)
		CHECK(requests_allowed(source(STYLE_STREET), stalled) == 2);
	CHECK(requests_allowed(source(STYLE_SATELLITE), 0) == 2);
	CHECK(requests_allowed(source(STYLE_SATELLITE), 1) == 3);
	CHECK(requests_allowed(source(STYLE_SATELLITE), 4) == 6);
	CHECK(requests_allowed(source(STYLE_SATELLITE), 9) == 6);
	CHECK(requests_allowed(source(STYLE_SATELLITE), -1) == 2);
}

int main() {
	test_requests_allowed();
	test_tile_zoom();
	test_visible_tiles();
	test_ancestors();
	test_urls();
	test_cache_names();
	test_meta();
	test_refresh_rules();
	test_headers();
	test_evictions();
	test_launch_area();
	test_launch_tiles();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
