// The parts of TileFetcher that don't need a network: off Windows every request just fails, which is
// enough to see requests come back, repeated ones merged, unwanted ones dropped, and the cache trimmed.
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>

#include "file_io.h"
#include "tile_fetcher.h"

using namespace map_tiles;

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

static TileRequest make_request(int x) {
	TileRequest request;
	request.style = STYLE_STREET;
	request.key = TileKey(5, x, 3);
	request.url = expand_url(source(STYLE_STREET).url_template, request.key);
	return request;
}

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void test_results_and_dropping(const std::string& root) {
	TileFetcher fetcher(root, TileFetcher::DEFAULT_CACHE_LIMIT_BYTES);
	// Asking for the same tile again and again is one request.
	fetcher.begin_frame();
	for (int repeat = 0; repeat < 4; repeat++)
		fetcher.want(make_request(1));
	std::vector<std::pair<int, TileKey> > dropped;
	fetcher.end_frame(&dropped);
	CHECK(dropped.empty());
	int arrived = 0;
	for (int wait = 0; wait < 200 && arrived == 0; wait++) {
		TileResult result;
		while (fetcher.next_result(&result)) {
			arrived++;
			CHECK(result.out_at_start == 1);
			CHECK(result.key == TileKey(5, 1, 3));
			CHECK(result.outcome == TileResult::FAILED);
		}
		sleep_ms(10);
	}
	CHECK(arrived == 1);
	sleep_ms(50);
	TileResult extra;
	CHECK(!fetcher.next_result(&extra));
	CHECK(fetcher.busy_count() == 0);

	// A frame that wants fewer tiles than the one before drops what it no longer wants, if it hasn't started.
	// (With two workers busy it's the later ones that are still waiting; whichever are dropped are
	// reported once, and every tile is either dropped or comes back.)
	fetcher.begin_frame();
	for (int x = 10; x < 20; x++)
		fetcher.want(make_request(x));
	fetcher.end_frame(&dropped);
	CHECK(dropped.empty());
	fetcher.begin_frame();
	fetcher.want(make_request(10));
	dropped.clear();
	fetcher.end_frame(&dropped);
	std::vector<TileKey> returned;
	for (int wait = 0; wait < 300; wait++) {
		TileResult result;
		while (fetcher.next_result(&result)) {
			returned.push_back(result.key);
			CHECK(result.out_at_start >= 1 && result.out_at_start <= 2);   // Street tiles: never more than two out.
		}
		if (fetcher.busy_count() == 0)
			break;
		sleep_ms(10);
	}
	TileResult result;
	while (fetcher.next_result(&result)) {
		returned.push_back(result.key);
		CHECK(result.out_at_start >= 1 && result.out_at_start <= 2);
	}
	// Each of the nine tiles no longer wanted either never ran (dropped, reported once) or came back once.
	// (Off Windows a request fails at once, so a worker may have finished tile 10 before the second frame;
	// asking for it again is then a new request, and it comes back twice. One is the least it can do.)
	for (int x = 11; x < 20; x++) {
		int dropped_count = 0;
		int returned_count = 0;
		for (size_t i = 0; i < dropped.size(); i++) {
			if (dropped[i].second == TileKey(5, x, 3))
				dropped_count++;
		}
		for (size_t i = 0; i < returned.size(); i++) {
			if (returned[i] == TileKey(5, x, 3))
				returned_count++;
		}
		CHECK(dropped_count + returned_count == 1);
	}
	int still_wanted = 0;
	for (size_t i = 0; i < returned.size(); i++) {
		if (returned[i] == TileKey(5, 10, 3))
			still_wanted++;
	}
	CHECK(still_wanted >= 1 && still_wanted <= 2);
	for (size_t i = 0; i < dropped.size(); i++)
		CHECK(!(dropped[i].second == TileKey(5, 10, 3)));   // The one still wanted is not among them.
}

static void test_sources_independent(const std::string& root) {
	// Ten street tiles keep that source's two requests busy; a satellite one doesn't wait behind them.
	CHECK(map_tiles::source(STYLE_STREET).max_out <= TileFetcher::MAX_WORKERS);
	CHECK(map_tiles::source(STYLE_SATELLITE).max_out <= TileFetcher::MAX_WORKERS);
	TileFetcher fetcher(root, TileFetcher::DEFAULT_CACHE_LIMIT_BYTES);
	fetcher.begin_frame();
	for (int x = 30; x < 40; x++)
		fetcher.want(make_request(x));
	TileRequest satellite = make_request(50);
	satellite.style = STYLE_SATELLITE;
	fetcher.want(satellite);
	std::vector<std::pair<int, TileKey> > dropped;
	fetcher.end_frame(&dropped);
	CHECK(dropped.empty());
	std::vector<TileResult> results;
	for (int wait = 0; wait < 300; wait++) {
		TileResult result;
		while (fetcher.next_result(&result))
			results.push_back(result);
		if (fetcher.busy_count() == 0)
			break;
		sleep_ms(10);
	}
	TileResult result;
	while (fetcher.next_result(&result))
		results.push_back(result);
	CHECK(results.size() == 11);
	int satellite_place = -1;
	for (size_t i = 0; i < results.size(); i++) {
		if (results[i].style == STYLE_SATELLITE)
			satellite_place = (int)i;
	}
	CHECK(satellite_place >= 0 && satellite_place < 9);   // Not last: it started while the street tiles were still going.
}

static void write_file(const std::string& path, size_t bytes) {
	fileio::make_directories(fileio::directory_of(path));
	std::string error;
	fileio::write_text_atomic(path, std::string(bytes, 'x'), &error);
}

static void test_trim(const std::string& root) {
	// Six tiles of 100 bytes (picture 90 + details 10), written one after another, and a stray details file.
	const std::string cache = fileio::join(root, "cache");
	for (int i = 0; i < 6; i++) {
		char name[64];
		snprintf(name, sizeof(name), "osm/5/%d/3", i);
		write_file(fileio::join(cache, std::string(name) + ".png"), 90);
		write_file(fileio::join(cache, std::string(name) + ".meta"), 10);
		sleep_ms(15);
	}
	write_file(fileio::join(cache, "osm/5/9/3.meta"), 10);
	{
		// Held to 250 bytes: the four oldest tiles (0 to 3) go, leaving 4 and 5 (200 bytes), and so does the stray details file.
		TileFetcher fetcher(cache, 250);
		fetcher.begin_frame();
		fetcher.want(make_request(1));   // Starts the workers, one of which trims.
		fetcher.end_frame(NULL);
		for (int wait = 0; wait < 200; wait++) {
			if (!fileio::file_exists(fileio::join(cache, "osm/5/0/3.png")) && !fileio::file_exists(fileio::join(cache, "osm/5/3/3.png")))
				break;
			sleep_ms(10);
		}
	}
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/0/3.png")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/0/3.meta")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/1/3.png")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/2/3.png")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/3/3.png")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/3/3.meta")));
	CHECK(fileio::file_exists(fileio::join(cache, "osm/5/4/3.png")));
	CHECK(fileio::file_exists(fileio::join(cache, "osm/5/4/3.meta")));
	CHECK(fileio::file_exists(fileio::join(cache, "osm/5/5/3.png")));
	CHECK(!fileio::file_exists(fileio::join(cache, "osm/5/9/3.meta")));
}

static void test_file_helpers(const std::string& root) {
	const std::string deep = fileio::join(root, "a/b/c");
	CHECK(fileio::make_directories(deep));
	CHECK(fileio::make_directories(deep));   // Already there.
	write_file(fileio::join(deep, "one.txt"), 5);
	write_file(fileio::join(root, "a/two.txt"), 7);
	std::vector<fileio::FileEntry> files;
	fileio::list_files(fileio::join(root, "a"), &files);
	CHECK(files.size() == 2);
	int64_t total = 0;
	for (size_t i = 0; i < files.size(); i++)
		total += files[i].bytes;
	CHECK(total == 12);
	files.clear();
	fileio::list_files(fileio::join(root, "nothing"), &files);
	CHECK(files.empty());
}

int main(int argc, char** argv) {
	std::string root = argc > 1 ? argv[1] : "/tmp";
	root = fileio::join(root, "tile_fetcher_test_" + std::to_string((long)getpid()));
	fileio::make_directories(root);
	test_file_helpers(root);
	test_results_and_dropping(fileio::join(root, "none"));
	test_sources_independent(fileio::join(root, "none2"));
	test_trim(root);
	// (The folder is left for the OS to clear; the tests have no way to delete folders.)
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
