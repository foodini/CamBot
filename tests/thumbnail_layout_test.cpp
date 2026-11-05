// The arithmetic behind the pictures on the timeline's clips: where they go, what they show, and
// which are kept.
#include <math.h>
#include <stdio.h>

#include "thumbnail_layout.h"

using namespace thumbnails;

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va = (a), vb = (b); if (fabs(va - vb) > (tol)) { printf("FAIL line %d: %s = %.9f, wanted %.9f\n", __LINE__, #a, va, vb); g_failures++; } } while (0)

static void test_keyframes() {
	std::vector<int64_t> keyframes;
	keyframes.push_back(0);
	keyframes.push_back(300);
	keyframes.push_back(900);
	keyframes.push_back(1000);
	CHECK(keyframe_for(keyframes, 0, 100) == 0);
	CHECK(keyframe_for(keyframes, 299, 100) == 0);
	CHECK(keyframe_for(keyframes, 300, 100) == 300);
	CHECK(keyframe_for(keyframes, 899, 100) == 300);
	CHECK(keyframe_for(keyframes, 900, 100) == 900);
	CHECK(keyframe_for(keyframes, 99999, 100) == 1000);
	CHECK(keyframe_for(keyframes, -5, 100) == 0);   // Before them all: the first.

	std::vector<int64_t> none;
	CHECK(keyframe_for(none, 250, 100) == 200);
	CHECK(keyframe_for(none, 99, 100) == 0);
	CHECK(keyframe_for(none, -3, 100) == 0);
	CHECK(keyframe_for(none, 250, 0) == 250);

	std::vector<int64_t> later;
	later.push_back(500);
	CHECK(keyframe_for(later, 10, 100) == 500);   // The first one, though it is after.
}

static void test_visible_tiles() {
	// A clip from 100 to 460, tiles 100 wide, all of it in view: four tiles, the last with 60 of its 100 showing.
	std::vector<TileSlot> tiles = visible_tiles(100.0, 460.0, 0.0, 1000.0, 100.0, 100);
	CHECK(tiles.size() == 4);
	if (tiles.size() == 4) {
		CHECK(tiles[0].index == 0);
		CHECK_NEAR(tiles[0].left, 100.0, 1e-9);
		CHECK_NEAR(tiles[0].right, 200.0, 1e-9);
		CHECK_NEAR(tiles[0].shown, 1.0, 1e-9);
		CHECK_NEAR(tiles[3].left, 400.0, 1e-9);
		CHECK_NEAR(tiles[3].right, 460.0, 1e-9);
		CHECK_NEAR(tiles[3].shown, 0.6, 1e-9);
	}
	// A clip that ends exactly on a tile edge has no sliver of a fifth.
	tiles = visible_tiles(0.0, 300.0, -50.0, 1000.0, 100.0, 100);
	CHECK(tiles.size() == 3);
	// Only the part in view: the clip starts far to the left, so the first picture shown is a later one, in the same place it always is.
	tiles = visible_tiles(-10000.0, 5000.0, 0.0, 250.0, 100.0, 100);
	CHECK(tiles.size() == 3);
	if (tiles.size() == 3) {
		CHECK(tiles[0].index == 100);
		CHECK_NEAR(tiles[0].left, 0.0, 1e-9);
		CHECK(tiles[2].index == 102);
		CHECK_NEAR(tiles[2].left, 200.0, 1e-9);
	}
	// One that begins partly before the view.
	tiles = visible_tiles(-30.0, 5000.0, 0.0, 100.0, 100.0, 100);
	CHECK(tiles.size() == 2);   // The one from -30 to 70, and the one from 70 on.
	if (tiles.size() == 2) {
		CHECK(tiles[0].index == 0);
		CHECK(tiles[1].index == 1);
	}
	// Nothing in view, or a clip with no width, or no room asked for.
	CHECK(visible_tiles(0.0, 100.0, 200.0, 300.0, 50.0, 10).empty());
	CHECK(visible_tiles(0.0, 0.0, -10.0, 300.0, 50.0, 10).empty());
	CHECK(visible_tiles(0.0, 100.0, 0.0, 300.0, 50.0, 0).empty());
	CHECK(visible_tiles(0.0, 100.0, 0.0, 300.0, 0.0, 10).empty());
	// A limit on how many: the first from the left.
	tiles = visible_tiles(0.0, 10000.0, 0.0, 10000.0, 10.0, 7);
	CHECK(tiles.size() == 7);
	if (tiles.size() == 7)
		CHECK(tiles[6].index == 6);
}

static void test_tile_ticks() {
	// A clip of 10000 ticks starting at 5000 into its file, 2 ticks a pixel, tiles 100 pixels wide.
	CHECK(tile_ticks(5000, 15000, 0, 100.0, 2.0) == 5000);
	CHECK(tile_ticks(5000, 15000, 1, 100.0, 2.0) == 5200);
	CHECK(tile_ticks(5000, 15000, 3, 100.0, 2.0) == 5600);
	// Never past the clip's last tick.
	CHECK(tile_ticks(5000, 15000, 100, 100.0, 2.0) == 14999);
	// Fractions of a tick are dropped, not rounded up.
	CHECK(tile_ticks(0, 100000, 1, 100.0, 0.5555) == 55);
}

static void test_catalog() {
	Catalog catalog;
	unsigned texture = 0;
	catalog.add(ThumbKey(0, 100, false), 11, 1);
	catalog.add(ThumbKey(0, 500, false), 12, 1);
	catalog.add(ThumbKey(1, 300, false), 13, 1);
	catalog.add(ThumbKey(0, 100, true), 14, 1);
	CHECK(catalog.count() == 4);
	CHECK(catalog.contains(ThumbKey(0, 500, false)));
	CHECK(!catalog.contains(ThumbKey(0, 500, true)));

	CHECK(catalog.find(ThumbKey(0, 500, false), 2, &texture) && texture == 12);
	CHECK(!catalog.find(ThumbKey(0, 400, false), 2, &texture));
	CHECK(catalog.find(ThumbKey(0, 100, true), 2, &texture) && texture == 14);

	// The nearest of the same file, never another's.
	CHECK(catalog.nearest(0, 450, 2, &texture) && texture == 12);
	CHECK(catalog.nearest(0, 150, 2, &texture) && (texture == 11 || texture == 14));
	CHECK(catalog.nearest(0, 99999, 2, &texture) && texture == 12);
	CHECK(catalog.nearest(1, 0, 2, &texture) && texture == 13);
	CHECK(!catalog.nearest(2, 0, 2, &texture));

	// Adding the same key again replaces it.
	catalog.add(ThumbKey(0, 500, false), 99, 3);
	CHECK(catalog.count() == 4);
	CHECK(catalog.find(ThumbKey(0, 500, false), 3, &texture) && texture == 99);
}

static void test_eviction() {
	Catalog catalog;
	for (int i = 0; i < 6; i++)
		catalog.add(ThumbKey(0, i * 100, false), 100 + i, 1 + i);   // Used in frames 1 to 6, in order.
	unsigned texture = 0;
	catalog.find(ThumbKey(0, 0, false), 7, &texture);   // The oldest is used again, so it is now the newest.
	// Down to four, in frame 8: the two least recently used go (tiles 1 and 2), oldest first.
	std::vector<unsigned> removed = catalog.evict(4, 8);
	CHECK(removed.size() == 2);
	if (removed.size() == 2) {
		CHECK(removed[0] == 101);
		CHECK(removed[1] == 102);
	}
	CHECK(catalog.count() == 4);
	CHECK(!catalog.contains(ThumbKey(0, 100, false)));
	CHECK(catalog.contains(ThumbKey(0, 0, false)));
	// Those used in the frame being drawn are never taken, even over the limit.
	removed = catalog.evict(1, 7);   // Frame 7: the one used in 7 stays, and so do those that count as used then or later... only older ones go.
	CHECK(catalog.contains(ThumbKey(0, 0, false)));
	CHECK(catalog.count() == 1);
	removed = catalog.evict(0, 7);
	CHECK(removed.empty());
	CHECK(catalog.count() == 1);
}

int main() {
	test_keyframes();
	test_visible_tiles();
	test_tile_ticks();
	test_catalog();
	test_eviction();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures ? 1 : 0;
}
