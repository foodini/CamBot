#include <math.h>
#include <stdio.h>

#include "scan_gesture.h"
#include "timeline_scale.h"

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static TimelineScale make(int64_t total, double min_tpp, float width) {
	TimelineScale scale;
	scale.set_extent(total, min_tpp);
	scale.set_width(width);
	return scale;
}

static void test_fit() {
	TimelineScale scale = make(300000, 10.0, 1000.0f);   // 300 ticks a pixel when fitted
	CHECK(scale.is_fitted());
	CHECK(near(scale.ticks_per_pixel(), 300.0, 1e-9));
	CHECK(near(scale.tick_to_x(0), 0.0, 1e-6));
	CHECK(near(scale.tick_to_x(300000), 1000.0, 1e-3));
	CHECK(scale.x_to_timeline_ticks(-50.0f) == 0);
	CHECK(scale.x_to_timeline_ticks(5000.0f) == 300000);
	CHECK(scale.x_to_timeline_ticks(500.0f) == 150000);

	// Fitted stays fitted as the window or the timeline changes.
	scale.set_width(500.0f);
	CHECK(scale.is_fitted() && near(scale.ticks_per_pixel(), 600.0, 1e-9));
	scale.set_extent(150000, 10.0);
	CHECK(scale.is_fitted() && near(scale.ticks_per_pixel(), 300.0, 1e-9));

	// A timeline too short to zoom is shown as wide as allowed, from the start.
	TimelineScale tiny = make(100, 10.0, 1000.0f);
	CHECK(near(tiny.ticks_per_pixel(), 10.0, 1e-9));
	CHECK(tiny.view_start() == 0.0);
}

static void test_zoom_keeps_the_point_under_the_cursor() {
	TimelineScale scale = make(1000000, 1.0, 1000.0f);
	const float anchor_xs[] = { 0.0f, 100.0f, 500.0f, 937.0f, 1000.0f };
	for (int i = 0; i < 5; i++) {
		TimelineScale s = make(1000000, 1.0, 1000.0f);
		float x = anchor_xs[i];
		double before = s.x_to_ticks(x);
		s.zoom_at(x, 4.0);
		CHECK(!s.is_fitted());
		CHECK(near(s.ticks_per_pixel(), 250.0, 1e-9));
		// Unless pinned by an end of the timeline, the point stays under the cursor.
		double after = s.x_to_ticks(x);
		CHECK(near(before, after, 1e-6));
	}
	// Zooming out past everything gives back the fitted view.
	scale.zoom_at(300.0f, 8.0);
	scale.zoom_at(300.0f, 0.01);
	CHECK(scale.is_fitted());
	CHECK(scale.view_start() == 0.0);
	// And in as far as allowed and no further.
	scale.zoom_at(500.0f, 1e9);
	CHECK(near(scale.ticks_per_pixel(), 1.0, 1e-12));
	scale.zoom_at(500.0f, 1e9);
	CHECK(near(scale.ticks_per_pixel(), 1.0, 1e-12));
}

static void test_pan_stays_inside() {
	TimelineScale scale = make(1000000, 1.0, 1000.0f);
	scale.zoom_at(0.0f, 10.0);   // 100 ticks a pixel: 100000 ticks showing
	CHECK(near(scale.view_start(), 0.0, 1e-9));
	scale.pan_by_pixels(-300.0f);
	CHECK(scale.view_start() == 0.0);
	scale.pan_by_pixels(300.0f);
	CHECK(near(scale.view_start(), 30000.0, 1e-6));
	scale.pan_by_pixels(1e7f);
	CHECK(near(scale.view_end(), 1000000.0, 1e-3));
	CHECK(near(scale.view_start(), 900000.0, 1e-3));
	scale.fit();
	scale.pan_by_pixels(50.0f);   // nothing to pan to
	CHECK(scale.view_start() == 0.0);
}

static void test_reveal() {
	TimelineScale scale = make(1000000, 1.0, 1000.0f);
	scale.zoom_at(0.0f, 10.0);
	CHECK(!scale.reveal(50000.0));          // already showing
	CHECK(scale.view_start() == 0.0);
	CHECK(scale.reveal(500000.0));
	CHECK(near(scale.tick_to_x(500000.0), 250.0, 1e-3));   // a quarter in
	CHECK(scale.reveal(999000.0));          // near the end the view stops at the end
	CHECK(near(scale.view_end(), 1000000.0, 1e-3));
	CHECK(scale.reveal(10.0));
	CHECK(scale.view_start() == 0.0);
}

static void test_ruler() {
	// 90 000 ticks a second, 100 pixels to a second: marks every second would be 100 pixels apart.
	CHECK(timeline_ruler::step_seconds(0.01, 80.0) == 1.0);
	CHECK(timeline_ruler::step_seconds(0.01, 120.0) == 2.0);
	CHECK(timeline_ruler::step_seconds(1.0, 80.0) == 120.0);
	CHECK(timeline_ruler::step_seconds(0.0005, 80.0) == 0.05);
	CHECK(timeline_ruler::step_seconds(0.0, 80.0) == 0.01);
	CHECK(timeline_ruler::step_seconds(1e9, 80.0) == 14400.0);

	CHECK(timeline_ruler::label(0.0, 1.0) == "0:00");
	CHECK(timeline_ruler::label(65.0, 5.0) == "1:05");
	CHECK(timeline_ruler::label(3723.0, 1.0) == "1:02:03");
	CHECK(timeline_ruler::label(4.5, 0.5) == "0:04.5");
	CHECK(timeline_ruler::label(74.25, 0.05) == "1:14.25");
	CHECK(timeline_ruler::label(3723.5, 0.5) == "1:02:03.5");
	CHECK(timeline_ruler::label(0.99999999999, 1.0) == "0:01");
	CHECK(timeline_ruler::label(-3.0, 1.0) == "0:00");
}

static void test_scan_curve() {
	const float native = 30.0f;
	CHECK(scan_gesture::rate_for_offset(0.0f, native) == 0.0f);
	CHECK(scan_gesture::rate_for_offset(0.005f, native) == 0.0f);
	CHECK(scan_gesture::rate_for_offset(-0.005f, native) == 0.0f);
	float slow = scan_gesture::rate_for_offset(0.011f, native);
	CHECK(slow > 4.9f && slow < 6.0f);
	float fastest = scan_gesture::rate_for_offset(0.5f, native);
	CHECK(near(fastest, 50.0 * native, 1e-2));
	CHECK(scan_gesture::rate_for_offset(2.0f, native) == fastest);
	CHECK(scan_gesture::rate_for_offset(-0.3f, native) == -scan_gesture::rate_for_offset(0.3f, native));
	CHECK(scan_gesture::rate_for_offset(0.2f, native) < scan_gesture::rate_for_offset(0.3f, native));
	CHECK(near(scan_gesture::drag_for_pixels(960.0f), 1.0, 1e-6));
}

int main() {
	test_fit();
	test_zoom_keeps_the_point_under_the_cursor();
	test_pan_stays_inside();
	test_reveal();
	test_ruler();
	test_scan_curve();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
