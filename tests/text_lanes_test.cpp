// Where text boxes go on the timeline's text track, and dragging them there (text_lanes.h).
#include <stdio.h>

#include "text_lanes.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

using namespace text_lanes;

static void test_assign() {
	int count = -1;
	std::vector<Span> spans;
	std::vector<int> lanes = assign(spans, &count);
	CHECK(lanes.empty() && count == 0);

	spans.push_back(Span(0, 100));
	spans.push_back(Span(100, 200));    // Starts as the first ends: the same row.
	spans.push_back(Span(150, 300));    // Overlaps the second: row 1.
	spans.push_back(Span(250, 400));    // The second has gone by then (row 0 is free again).
	lanes = assign(spans, &count);
	CHECK(count == 2);
	CHECK(lanes[0] == 0 && lanes[1] == 0 && lanes[2] == 1 && lanes[3] == 0);

	// Given out of order, they are still dealt with by start, and the answers come back in the order given.
	spans.clear();
	spans.push_back(Span(500, 900));
	spans.push_back(Span(0, 600));
	spans.push_back(Span(100, 200));
	lanes = assign(spans, &count);
	CHECK(count == 2);
	CHECK(lanes[1] == 0 && lanes[2] == 1 && lanes[0] == 1);    // The third (100 to 200) has gone by when the first begins.

	// Three at once, the same start.
	spans.clear();
	spans.push_back(Span(10, 20));
	spans.push_back(Span(10, 20));
	spans.push_back(Span(10, 20));
	lanes = assign(spans, &count);
	CHECK(count == 3 && lanes[0] == 0 && lanes[1] == 1 && lanes[2] == 2);
}

static void test_dragged() {
	const Span span(1000, 2000);
	const int64_t timeline = 10000;
	const int64_t minimum = 40;

	// The body moves both ends, keeping the length, and stops at the ends of the timeline.
	Span moved = dragged(span, BODY, 500, timeline, minimum);
	CHECK(moved.start == 1500 && moved.end == 2500);
	moved = dragged(span, BODY, -5000, timeline, minimum);
	CHECK(moved.start == 0 && moved.end == 1000);
	moved = dragged(span, BODY, 50000, timeline, minimum);
	CHECK(moved.start == 9000 && moved.end == 10000);

	// The left end moves just that end, to the start of the timeline at the earliest and a least length from the other end.
	moved = dragged(span, LEFT_END, -300, timeline, minimum);
	CHECK(moved.start == 700 && moved.end == 2000);
	moved = dragged(span, LEFT_END, -5000, timeline, minimum);
	CHECK(moved.start == 0 && moved.end == 2000);
	moved = dragged(span, LEFT_END, 5000, timeline, minimum);
	CHECK(moved.start == 1960 && moved.end == 2000);

	// The right end likewise.
	moved = dragged(span, RIGHT_END, 300, timeline, minimum);
	CHECK(moved.start == 1000 && moved.end == 2300);
	moved = dragged(span, RIGHT_END, 50000, timeline, minimum);
	CHECK(moved.start == 1000 && moved.end == 10000);
	moved = dragged(span, RIGHT_END, -5000, timeline, minimum);
	CHECK(moved.start == 1000 && moved.end == 1040);

	// A span longer than the timeline: moved only as far as it must be.
	moved = dragged(Span(0, 12000), BODY, 100, timeline, minimum);
	CHECK(moved.start == 0 && moved.end == 12000);
	// One that runs past the end can still be shortened from its right end.
	moved = dragged(Span(9000, 12000), RIGHT_END, -500, timeline, minimum);
	CHECK(moved.start == 9000 && moved.end == 10000);
}

int main() {
	test_assign();
	test_dragged();
	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
