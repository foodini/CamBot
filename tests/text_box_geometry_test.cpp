// Moving and resizing a text box with the mouse (text_box_geometry.h).
#include <math.h>
#include <stdio.h>

#include "text_box_geometry.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)

using namespace text_box_geometry;

static bool near_rect(const Rect& r, float x, float y, float w, float h) {
	return fabsf(r.x - x) < 1e-5f && fabsf(r.y - y) < 1e-5f && fabsf(r.w - w) < 1e-5f && fabsf(r.h - h) < 1e-5f;
}

int main() {
	const Rect box(0.1f, 0.75f, 0.8f, 0.15f);

	// Moving.
	CHECK(near_rect(move(box, 0.0f, -0.25f), 0.1f, 0.5f, 0.8f, 0.15f));
	CHECK(near_rect(move(box, -0.5f, 0.0f), 0.0f, 0.75f, 0.8f, 0.15f));       // Stops at the left edge,
	CHECK(near_rect(move(box, 0.5f, 0.0f), 0.2f, 0.75f, 0.8f, 0.15f));        // the right,
	CHECK(near_rect(move(box, 0.0f, 0.5f), 0.1f, 0.85f, 0.8f, 0.15f));        // the bottom,
	CHECK(near_rect(move(box, 0.0f, -2.0f), 0.1f, 0.0f, 0.8f, 0.15f));        // and the top.
	// It comes back out when the mouse does (the drag is measured from the start).
	CHECK(near_rect(move(box, 0.0f, -0.1f), 0.1f, 0.65f, 0.8f, 0.15f));

	// Corners: the opposite one stays put.
	Rect r = drag_corner(box, TOP_LEFT, 0.1f, -0.25f);
	CHECK(near_rect(r, 0.2f, 0.5f, 0.7f, 0.4f));
	r = drag_corner(box, BOTTOM_RIGHT, -0.2f, 0.05f);
	CHECK(near_rect(r, 0.1f, 0.75f, 0.6f, 0.2f));
	r = drag_corner(box, TOP_RIGHT, -0.2f, 0.05f);
	CHECK(near_rect(r, 0.1f, 0.8f, 0.6f, 0.1f));
	r = drag_corner(box, BOTTOM_LEFT, -0.05f, 0.05f);
	CHECK(near_rect(r, 0.05f, 0.75f, 0.85f, 0.2f));

	// Not out of the frame.
	r = drag_corner(box, TOP_LEFT, -1.0f, -1.0f);
	CHECK(near_rect(r, 0.0f, 0.0f, 0.9f, 0.9f));
	r = drag_corner(box, BOTTOM_RIGHT, 1.0f, 1.0f);
	CHECK(near_rect(r, 0.1f, 0.75f, 0.9f, 0.25f));

	// Not squeezed away, nor turned inside out: dragging a corner past the opposite one stops at the least size.
	r = drag_corner(box, TOP_LEFT, 1.0f, 1.0f);
	CHECK(near_rect(r, 0.9f - MIN_WIDTH, 0.9f - MIN_HEIGHT, MIN_WIDTH, MIN_HEIGHT));
	r = drag_corner(box, BOTTOM_RIGHT, -1.0f, -1.0f);
	CHECK(near_rect(r, 0.1f, 0.75f, MIN_WIDTH, MIN_HEIGHT));

	// Edges: only that edge moves, and the sideways drag of a top edge (or the up and down of a left one) does nothing.
	r = drag_edge(box, EDGE_LEFT, 0.1f, 0.3f);
	CHECK(near_rect(r, 0.2f, 0.75f, 0.7f, 0.15f));
	r = drag_edge(box, EDGE_RIGHT, -0.2f, -0.3f);
	CHECK(near_rect(r, 0.1f, 0.75f, 0.6f, 0.15f));
	r = drag_edge(box, EDGE_TOP, 0.3f, -0.25f);
	CHECK(near_rect(r, 0.1f, 0.5f, 0.8f, 0.4f));
	r = drag_edge(box, EDGE_BOTTOM, 0.3f, 0.05f);
	CHECK(near_rect(r, 0.1f, 0.75f, 0.8f, 0.2f));
	// The frame and the least size limit them too.
	CHECK(near_rect(drag_edge(box, EDGE_LEFT, -1.0f, 0.0f), 0.0f, 0.75f, 0.9f, 0.15f));
	CHECK(near_rect(drag_edge(box, EDGE_RIGHT, 1.0f, 0.0f), 0.1f, 0.75f, 0.9f, 0.15f));
	CHECK(near_rect(drag_edge(box, EDGE_TOP, 0.0f, -2.0f), 0.1f, 0.0f, 0.8f, 0.9f));
	CHECK(near_rect(drag_edge(box, EDGE_BOTTOM, 0.0f, 2.0f), 0.1f, 0.75f, 0.8f, 0.25f));
	CHECK(near_rect(drag_edge(box, EDGE_LEFT, 5.0f, 0.0f), 0.9f - MIN_WIDTH, 0.75f, MIN_WIDTH, 0.15f));
	CHECK(near_rect(drag_edge(box, EDGE_TOP, 0.0f, 5.0f), 0.1f, 0.9f - MIN_HEIGHT, 0.8f, MIN_HEIGHT));

	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
