// Tests of frame_mapping: places in the picture and on the preview, with the project's rotation.

#include "frame_mapping.h"
#include <math.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); failures++; } } while (0)

static const double PI = 3.14159265358979323846;

static void test_unrotated() {
	double x, y;
	frame_mapping::picture_to_preview(0.25, 0.75, 0.0, 16.0 / 9.0, &x, &y);
	CHECK(fabs(x - 0.25) < 1e-12 && fabs(y - 0.75) < 1e-12);
	CHECK(fabs(frame_mapping::cover_scale(0.0, 16.0 / 9.0) - 1.0) < 1e-12);
}

static void test_half_turn() {
	// Turned over: the top left of the picture is the bottom right of the preview, with no zoom.
	double x, y;
	frame_mapping::picture_to_preview(0.0, 0.0, PI, 16.0 / 9.0, &x, &y);
	CHECK(fabs(x - 1.0) < 1e-9 && fabs(y - 1.0) < 1e-9);
	frame_mapping::picture_to_preview(0.5, 0.5, PI, 16.0 / 9.0, &x, &y);
	CHECK(fabs(x - 0.5) < 1e-9 && fabs(y - 0.5) < 1e-9);
}

static void test_zoom_covers() {
	// At any angle, every corner of the preview is a place inside the picture: nothing empty shows.
	const double aspect = 16.0 / 9.0;
	for (int step = -20; step <= 20; step++) {
		const double angle = step * PI / 40.0;
		const double corners[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
		for (int c = 0; c < 4; c++) {
			double u, v;
			frame_mapping::preview_to_picture(corners[c][0], corners[c][1], angle, aspect, &u, &v);
			CHECK(u >= -1e-9 && u <= 1.0 + 1e-9 && v >= -1e-9 && v <= 1.0 + 1e-9);
		}
	}
}

static void test_round_trip() {
	const double aspect = 16.0 / 9.0;
	for (int step = -10; step <= 10; step++) {
		const double angle = step * 0.173;
		for (double u = 0.0; u <= 1.0; u += 0.25) {
			for (double v = 0.0; v <= 1.0; v += 0.25) {
				double x, y, back_u, back_v;
				frame_mapping::picture_to_preview(u, v, angle, aspect, &x, &y);
				frame_mapping::preview_to_picture(x, y, angle, aspect, &back_u, &back_v);
				CHECK(fabs(back_u - u) < 1e-9 && fabs(back_v - v) < 1e-9);
			}
		}
	}
}

static void test_a_small_turn_is_a_turn_not_a_shear() {
	// The picture's own horizontal line (a row of pixels) stays straight and keeps its length in pixels, so two places a known
	// distance apart on the picture are the same distance apart on the preview times the zoom.
	const double aspect = 2.0;
	const double angle = 0.3;
	double x1, y1, x2, y2;
	frame_mapping::picture_to_preview(0.4, 0.5, angle, aspect, &x1, &y1);
	frame_mapping::picture_to_preview(0.6, 0.5, angle, aspect, &x2, &y2);
	// In picture pixels (height 1000, width 2000) the two are 0.2 * 2000 = 400 apart; on the preview (the same pixel size)
	// they are zoomed.
	const double dx = (x2 - x1) * 2000.0;
	const double dy = (y2 - y1) * 1000.0;
	const double zoom = frame_mapping::cover_scale(angle, aspect);
	CHECK(fabs(sqrt(dx * dx + dy * dy) - 400.0 * zoom) < 1e-6);
}

int main() {
	test_unrotated();
	test_half_turn();
	test_zoom_covers();
	test_round_trip();
	test_a_small_turn_is_a_turn_not_a_shear();
	if (failures == 0)
		printf("all ok\n");
	return failures == 0 ? 0 : 1;
}
