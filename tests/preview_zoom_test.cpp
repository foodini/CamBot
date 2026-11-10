// Tests of preview_zoom: the window onto the canvas that zooming on the preview moves about.

#include "preview_zoom.h"
#include <math.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static bool near(double a, double b) { return fabs(a - b) < 1e-9; }

static void test_whole_canvas() {
	preview_zoom::View view;
	double left, top, right, bottom;
	preview_zoom::region(view, &left, &top, &right, &bottom);
	CHECK(near(left, 0.0) && near(top, 0.0) && near(right, 1.0) && near(bottom, 1.0));
	CHECK(!view.zoomed());
	double x, y;
	preview_zoom::to_canvas(view, 0.3, 0.8, &x, &y);
	CHECK(near(x, 0.3) && near(y, 0.8));
}

static void test_zoom_keeps_the_place_under_the_mouse() {
	preview_zoom::View view;
	double canvas_x, canvas_y;
	// Zoom in about a place that isn't near an edge: what was under the mouse is still there.
	preview_zoom::to_canvas(view, 0.6, 0.45, &canvas_x, &canvas_y);
	view = preview_zoom::zoomed(view, 0.6, 0.45, 2.0);
	CHECK(near(view.zoom, 2.0));
	double x, y;
	preview_zoom::to_canvas(view, 0.6, 0.45, &x, &y);
	CHECK(near(x, canvas_x) && near(y, canvas_y));
	// And again, and then back out the same amount at the same place.
	view = preview_zoom::zoomed(view, 0.5, 0.5, 1.5);
	preview_zoom::to_canvas(view, 0.5, 0.5, &x, &y);
	view = preview_zoom::zoomed(view, 0.5, 0.5, 1.0 / 1.5);
	double x2, y2;
	preview_zoom::to_canvas(view, 0.5, 0.5, &x2, &y2);
	CHECK(near(x, x2) && near(y, y2));
}

static void test_limits() {
	preview_zoom::View view;
	// Out past the whole canvas is the whole canvas, and the window goes back to the middle.
	view = preview_zoom::zoomed(view, 0.1, 0.1, 0.5);
	CHECK(near(view.zoom, 1.0) && near(view.center_x, 0.5) && near(view.center_y, 0.5));
	// In as far as it goes.
	for (int i = 0; i < 40; i++)
		view = preview_zoom::zoomed(view, 0.9, 0.9, 1.5);
	CHECK(near(view.zoom, preview_zoom::MAX_ZOOM));
	double left, top, right, bottom;
	preview_zoom::region(view, &left, &top, &right, &bottom);
	CHECK(left >= -1e-9 && top >= -1e-9 && right <= 1.0 + 1e-9 && bottom <= 1.0 + 1e-9);
	// Zooming in at a corner stays on the canvas, with the corner still under the mouse.
	preview_zoom::View corner = preview_zoom::zoomed(preview_zoom::View(), 0.0, 0.0, 4.0);
	preview_zoom::region(corner, &left, &top, &right, &bottom);
	CHECK(near(left, 0.0) && near(top, 0.0) && near(right, 0.25) && near(bottom, 0.25));
}

static void test_pan() {
	preview_zoom::View view = preview_zoom::zoomed(preview_zoom::View(), 0.5, 0.5, 4.0);
	double canvas_x, canvas_y;
	preview_zoom::to_canvas(view, 0.5, 0.5, &canvas_x, &canvas_y);
	// Dragging the picture a tenth of the preview to the right shows what was a tenth of a window to the left.
	preview_zoom::View moved = preview_zoom::panned(view, 0.1, 0.0);
	double x, y;
	preview_zoom::to_canvas(moved, 0.5, 0.5, &x, &y);
	CHECK(near(x, canvas_x - 0.1 / 4.0) && near(y, canvas_y));
	// The place that was under the middle is now a tenth of the preview to the right of it.
	double px, py;
	preview_zoom::to_preview(moved, canvas_x, canvas_y, &px, &py);
	CHECK(near(px, 0.6) && near(py, 0.5));
	// Dragged far, it stops at the edge of the canvas.
	moved = preview_zoom::panned(view, 100.0, -100.0);
	double left, top, right, bottom;
	preview_zoom::region(moved, &left, &top, &right, &bottom);
	CHECK(near(left, 0.0) && near(bottom, 1.0));
}

static void test_round_trip() {
	preview_zoom::View view = preview_zoom::zoomed(preview_zoom::View(), 0.3, 0.7, 3.0);
	for (double x = 0.0; x <= 1.0; x += 0.25) {
		for (double y = 0.0; y <= 1.0; y += 0.25) {
			double cx, cy, px, py;
			preview_zoom::to_canvas(view, x, y, &cx, &cy);
			preview_zoom::to_preview(view, cx, cy, &px, &py);
			CHECK(near(px, x) && near(py, y));
		}
	}
}

static void test_followed() {
	// Zoom 4 about the middle: the window is the middle quarter, 0.375 to 0.625 on both axes.
	preview_zoom::View view = preview_zoom::zoomed(preview_zoom::View(), 0.5, 0.5, 4.0);
	// A place inside the margin leaves the view alone.
	preview_zoom::View same = preview_zoom::followed(view, 0.55, 0.45, 0.15);
	CHECK(near(same.center_x, 0.5) && near(same.center_y, 0.5) && near(same.zoom, 4.0));
	// One that has gone out of the window to the right and below is brought back to the margin: 0.15 of the window width (0.25) is 0.0375.
	preview_zoom::View moved = preview_zoom::followed(view, 0.7, 0.8, 0.15);
	double px, py;
	preview_zoom::to_preview(moved, 0.7, 0.8, &px, &py);
	CHECK(near(px, 0.85) && near(py, 0.85));
	// And the other way, to the left and above.
	moved = preview_zoom::followed(view, 0.2, 0.1, 0.15);
	preview_zoom::to_preview(moved, 0.2, 0.1, &px, &py);
	CHECK(near(px, 0.15) && near(py, 0.15));
	// Between the margin and the edge it also moves, only as far as the margin.
	moved = preview_zoom::followed(view, 0.6, 0.5, 0.15);
	preview_zoom::to_preview(moved, 0.6, 0.5, &px, &py);
	CHECK(near(px, 0.85) && near(py, 0.5));
	// Near the edge of the canvas the window stops at the canvas, so the place may be nearer the window's edge than the margin.
	moved = preview_zoom::followed(view, 1.0, 1.0, 0.15);
	double left, top, right, bottom;
	preview_zoom::region(moved, &left, &top, &right, &bottom);
	CHECK(near(right, 1.0) && near(bottom, 1.0));
}

int main() {
	test_whole_canvas();
	test_followed();
	test_zoom_keeps_the_place_under_the_mouse();
	test_limits();
	test_pan();
	test_round_trip();
	if (g_failures == 0)
		printf("all ok\n");
	return g_failures == 0 ? 0 : 1;
}
