#include <math.h>
#include <stdio.h>

#include "map_view.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va = (a), vb = (b); if (fabs(va - vb) > (tol)) { printf("FAIL line %d: %s = %.9f, wanted %.9f\n", __LINE__, #a, va, vb); g_failures++; } } while (0)

static MapViewport make_view() {
	MapViewport view;
	view.set_size(800.0f, 600.0f);
	view.set_view(map_projection::from_lat_lon(37.78, -122.51), 15.0);
	return view;
}

static void test_projection() {
	MapPoint origin = map_projection::from_lat_lon(0.0, 0.0);
	CHECK_NEAR(origin.x, 0.5, 1e-12);
	CHECK_NEAR(origin.y, 0.5, 1e-12);
	CHECK_NEAR(map_projection::from_lat_lon(0.0, -180.0).x, 0.0, 1e-12);
	CHECK_NEAR(map_projection::from_lat_lon(0.0, 180.0).x, 1.0, 1e-12);
	// North is up the screen: a smaller y. The projection's edge is at the top and bottom of the square.
	CHECK(map_projection::from_lat_lon(10.0, 0.0).y < 0.5);
	CHECK_NEAR(map_projection::from_lat_lon(85.0511287798, 0.0).y, 0.0, 1e-6);
	CHECK_NEAR(map_projection::from_lat_lon(89.9, 0.0).y, map_projection::from_lat_lon(85.0511287798, 0.0).y, 1e-12);
	// Known tile: Fort Funston is in OSM tile 10/163/396 or near; at zoom 10 the world is 1024 tiles wide.
	MapPoint funston = map_projection::from_lat_lon(37.7167, -122.5025);
	CHECK((int)floor(funston.x * 1024.0) == 164 || (int)floor(funston.x * 1024.0) == 163);
	CHECK((int)floor(funston.y * 1024.0) == 395 || (int)floor(funston.y * 1024.0) == 396);
}

static void test_screen_mapping() {
	MapViewport view = make_view();
	// The center is the middle of the pane.
	ScreenPoint middle = view.to_screen(view.center());
	CHECK_NEAR(middle.x, 400.0, 1e-3);
	CHECK_NEAR(middle.y, 300.0, 1e-3);
	MapPoint world = view.to_world(123.0f, 456.0f);
	ScreenPoint back = view.to_screen(world);
	CHECK_NEAR(back.x, 123.0, 1e-2);
	CHECK_NEAR(back.y, 456.0, 1e-2);
	// A zoom level is a doubling.
	CHECK_NEAR(view.scale(), 256.0 * 32768.0, 1.0);
}

static void test_zoom_and_pan() {
	MapViewport view = make_view();
	MapPoint under = view.to_world(650.0f, 120.0f);
	view.zoom_about(650.0f, 120.0f, 1.5);
	CHECK_NEAR(view.zoom(), 16.5, 1e-9);
	ScreenPoint s = view.to_screen(under);
	CHECK_NEAR(s.x, 650.0, 1e-2);
	CHECK_NEAR(s.y, 120.0, 1e-2);
	view.zoom_about(10.0f, 10.0f, 100.0);
	CHECK_NEAR(view.zoom(), map_zoom_limits::MAX_ZOOM, 1e-9);
	view.zoom_about(10.0f, 10.0f, -100.0);
	CHECK_NEAR(view.zoom(), map_zoom_limits::MIN_ZOOM, 1e-9);

	// Panning moves the map along with the mouse.
	view = make_view();
	MapPoint place = view.to_world(100.0f, 100.0f);
	view.pan_pixels(10.0f, -5.0f);
	ScreenPoint moved = view.to_screen(place);
	CHECK_NEAR(moved.x, 110.0, 1e-2);
	CHECK_NEAR(moved.y, 95.0, 1e-2);
}

static void test_fit() {
	MapViewport view;
	view.set_size(800.0f, 600.0f);
	MapPoint low = map_projection::from_lat_lon(37.80, -122.52);
	MapPoint high = map_projection::from_lat_lon(37.70, -122.45);
	view.fit(low, high, 0.1);
	// Everything is inside the pane with the margin kept, and it is as big as that allows.
	MapPoint corners[4] = { MapPoint(low.x, low.y), MapPoint(high.x, low.y), MapPoint(low.x, high.y), MapPoint(high.x, high.y) };
	bool touches = false;
	for (int i = 0; i < 4; i++) {
		ScreenPoint s = view.to_screen(corners[i]);
		CHECK(s.x >= 79.9f && s.x <= 720.1f && s.y >= 59.9f && s.y <= 540.1f);
		if (fabsf(s.x - 80.0f) < 1.0f || fabsf(s.x - 720.0f) < 1.0f || fabsf(s.y - 60.0f) < 1.0f || fabsf(s.y - 540.0f) < 1.0f)
			touches = true;
	}
	CHECK(touches);
	// One place on its own isn't zoomed in to the limit.
	view.fit(low, low, 0.1);
	CHECK(view.zoom() <= map_zoom_limits::MAX_FIT_ZOOM);
	CHECK_NEAR(view.center().x, low.x, 1e-12);
}

static void test_keep_in_view() {
	MapViewport view = make_view();
	MapPoint here = view.center();
	// In the middle: nothing moves.
	CHECK(!view.keep_in_view(here, 0.15));
	CHECK_NEAR(view.center().x, here.x, 1e-15);
	// A little outside the held-back edge: it moves just far enough to put the point on that edge.
	MapPoint near_edge = view.to_world(790.0f, 300.0f);
	CHECK(view.keep_in_view(near_edge, 0.15));
	ScreenPoint s = view.to_screen(near_edge);
	CHECK_NEAR(s.x, 800.0 * 0.85, 0.01);
	CHECK_NEAR(s.y, 300.0, 0.01);
	// Off the pane altogether: it is put in the middle.
	view = make_view();
	MapPoint far_away = view.to_world(5000.0f, -3000.0f);
	CHECK(view.keep_in_view(far_away, 0.15));
	ScreenPoint c = view.to_screen(far_away);
	CHECK_NEAR(c.x, 400.0, 0.01);
	CHECK_NEAR(c.y, 300.0, 0.01);
}

static void test_in_inset() {
	MapViewport view = make_view();
	CHECK(view.in_inset(view.center(), 0.25));
	CHECK(view.in_inset(view.to_world(210.0f, 160.0f), 0.25));    // Just inside the quarter-way lines.
	CHECK(!view.in_inset(view.to_world(190.0f, 300.0f), 0.25));   // Just outside.
	CHECK(!view.in_inset(view.to_world(400.0f, 460.0f), 0.25));
	CHECK(!view.in_inset(view.to_world(-10.0f, 300.0f), 0.0));    // Off the pane is never inside.
	CHECK(view.in_inset(view.to_world(5.0f, 5.0f), 0.0));
}

static void test_trace_positions() {
	MapTrace trace;
	trace.add(37.0, -122.0);
	trace.add(0.0, 0.0);      // No fix.
	trace.add(0.0, 0.0);
	trace.add(37.1, -122.1);
	trace.add(37.2, -122.2);
	CHECK(trace.count() == 5);
	CHECK(trace.valid(0) && !trace.valid(1) && !trace.valid(2) && trace.valid(3));
	MapPoint low, high;
	CHECK(trace.bounds(&low, &high));
	CHECK(low.x < high.x && low.y < high.y);

	MapPoint p;
	// In between two good samples it is in between.
	CHECK(trace.position_at(3.5, &p));
	CHECK_NEAR(p.x, (trace.point(3).x + trace.point(4).x) / 2.0, 1e-12);
	// On a sample with no fix, the one before it.
	CHECK(trace.position_at(1.0, &p));
	CHECK_NEAR(p.x, trace.point(0).x, 1e-12);
	// Past either end it is held at the end.
	CHECK(trace.position_at(99.0, &p));
	CHECK_NEAR(p.x, trace.point(4).x, 1e-12);
	CHECK(trace.position_at(-4.0, &p));
	CHECK_NEAR(p.x, trace.point(0).x, 1e-12);

	// Nothing but no-fix samples has no position.
	MapTrace empty;
	CHECK(!empty.position_at(0.0, &p));
	empty.add(0.0, 0.0);
	CHECK(!empty.position_at(0.0, &p));
	CHECK(!empty.bounds(&low, &high));
	// Sentinel garbage counts as no fix.
	empty.add(123.0, 10.0);
	CHECK(!empty.valid(1));
}

static void test_polyline() {
	MapViewport view = make_view();
	MapTrace trace;
	// A straight line of 1000 samples, 0.1 pixel apart, then a jump far off the pane and back.
	MapPoint start = view.to_world(100.0f, 300.0f);
	const double per_pixel = 1.0 / view.scale();
	for (int i = 0; i < 1000; i++) {
		MapPoint q(start.x + per_pixel * 0.1 * i, start.y);
		// Back to degrees through the inverse Mercator is not needed: use latitude/longitude directly.
		double longitude = q.x * 360.0 - 180.0;
		double n = 3.14159265358979323846 * (1.0 - 2.0 * q.y);
		double latitude = atan(sinh(n)) * 180.0 / 3.14159265358979323846;
		trace.add(latitude, longitude);
	}
	MapPolyline line;
	trace.build_polyline(0, 999, view, 2.0f, 50.0f, &line);
	CHECK(line.run_ends.size() == 1);
	// 100 pixels long at 2 pixels a point: about 50 points, not 1000.
	CHECK(line.points.size() >= 45 && line.points.size() <= 60);
	// The ends are where the first and last samples are.
	CHECK_NEAR(line.points.front().x, 100.0, 0.01);
	CHECK_NEAR(line.points.back().x, 200.0 - 0.1, 0.5);
	for (size_t i = 1; i + 1 < line.points.size(); i++) {
		float dx = line.points[i].x - line.points[i - 1].x;
		CHECK(dx >= 1.99f);
	}
	// Part of it: only that part.
	MapPolyline half;
	trace.build_polyline(0, 499, view, 2.0f, 50.0f, &half);
	CHECK(half.points.size() < line.points.size());
	CHECK_NEAR(half.points.back().x, 150.0, 0.5);
	// Nothing, or nothing valid, is an empty line.
	MapPolyline none;
	trace.build_polyline(5, 4, view, 2.0f, 50.0f, &none);
	CHECK(none.points.empty() && none.run_ends.empty());
}

static void test_polyline_culling() {
	MapViewport view = make_view();
	MapTrace trace;
	// A line along the middle of the pane, then a long way off and another stretch there, then back.
	for (int i = 0; i < 5; i++) {
		MapPoint q = view.to_world(100.0f + 50.0f * i, 300.0f);
		trace.add(atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * q.y))) * 180.0 / 3.14159265358979323846, q.x * 360.0 - 180.0);
	}
	for (int i = 0; i < 6; i++) {
		MapPoint q = view.to_world(5000.0f + 50.0f * i, 300.0f);
		trace.add(atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * q.y))) * 180.0 / 3.14159265358979323846, q.x * 360.0 - 180.0);
	}
	for (int i = 0; i < 5; i++) {
		MapPoint q = view.to_world(300.0f + 50.0f * i, 400.0f);
		trace.add(atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * q.y))) * 180.0 / 3.14159265358979323846, q.x * 360.0 - 180.0);
	}
	MapPolyline line;
	trace.build_polyline(0, trace.count() - 1, view, 2.0f, 50.0f, &line);
	// Two runs on the pane; the stretch far away is gone, except the points either side of it
	// that the lines to it start from. (The corners 50 pixels apart have points added to curve them.)
	CHECK(line.run_ends.size() == 2);
	CHECK(line.points.size() >= 10);
	CHECK(line.climb.size() == line.points.size());
	// (The points a line to somewhere far away starts from are kept: one at each end of the stretch that is gone.)
	int far_points = 0;
	for (size_t i = 0; i < line.points.size(); i++) {
		if (line.points[i].x > 1000.0f)
			far_points++;
	}
	CHECK(far_points <= 2);
	int previous = 0;
	for (size_t r = 0; r < line.run_ends.size(); r++) {
		CHECK(line.run_ends[r] > previous);
		previous = line.run_ends[r];
	}
	CHECK(previous == (int)line.points.size());
}

static MapTrace line_of(const MapViewport& view, const float xs[], const float ys[], const float climbs[], int count) {
	MapTrace trace;
	for (int i = 0; i < count; i++) {
		MapPoint q = view.to_world(xs[i], ys[i]);
		trace.add(atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * q.y))) * 180.0 / 3.14159265358979323846, q.x * 360.0 - 180.0, climbs[i]);
	}
	return trace;
}

static void test_curves() {
	MapViewport view = make_view();
	// Three points 40 pixels apart, with a bend at the middle one.
	const float xs[3] = { 100.0f, 140.0f, 140.0f };
	const float ys[3] = { 300.0f, 300.0f, 340.0f };
	const float climbs[3] = { 0.0f, 500.0f, 1000.0f };
	MapTrace trace = line_of(view, xs, ys, climbs, 3);
	MapPolyline line;
	trace.build_polyline(0, 2, view, 2.0f, 50.0f, &line);
	CHECK(line.run_ends.size() == 1);
	// About 14 points a leg now, not 3, and the original points are still on the line.
	CHECK(line.points.size() >= 24 && line.points.size() <= 34);
	bool found_first = false, found_middle = false, found_last = false;
	for (size_t i = 0; i < line.points.size(); i++) {
		if (fabsf(line.points[i].x - 100.0f) < 0.05f && fabsf(line.points[i].y - 300.0f) < 0.05f) found_first = true;
		if (fabsf(line.points[i].x - 140.0f) < 0.05f && fabsf(line.points[i].y - 300.0f) < 0.05f) found_middle = true;
		if (fabsf(line.points[i].x - 140.0f) < 0.05f && fabsf(line.points[i].y - 340.0f) < 0.05f) found_last = true;
	}
	CHECK(found_first && found_middle && found_last);
	// The climb rate runs along with it, from one end's to the other's.
	CHECK_NEAR(line.climb.front(), 0.0, 1e-3);
	CHECK_NEAR(line.climb.back(), 1000.0, 1e-3);
	for (size_t i = 1; i < line.climb.size(); i++)
		CHECK(line.climb[i] >= line.climb[i - 1] - 1e-3f);
	// The curve stays near the corner (a spline overshoots a little, a few pixels): no point strays far from the box around the three.
	for (size_t i = 0; i < line.points.size(); i++)
		CHECK(line.points[i].x >= 97.0f && line.points[i].x <= 148.0f && line.points[i].y >= 295.0f && line.points[i].y <= 343.0f);

	// A jump too long to be a curve stays a straight line.
	const float jump_xs[2] = { 100.0f, 700.0f };
	const float jump_ys[2] = { 300.0f, 300.0f };
	const float jump_climbs[2] = { 0.0f, 0.0f };
	MapTrace jump = line_of(view, jump_xs, jump_ys, jump_climbs, 2);
	jump.build_polyline(0, 1, view, 2.0f, 1000.0f, &line);
	CHECK(line.points.size() == 2);
}

static void test_keep_in_view_with_bias() {
	MapViewport view = make_view();
	// With the allowed area moved 100 pixels to the left, a point at the old right edge is already outside it.
	MapPoint point = view.to_world(560.0f, 300.0f);   // 800 * 0.75 - 40: inside the plain area.
	MapViewport plain = view;
	CHECK(!plain.keep_in_view(point, 0.25));
	CHECK(view.keep_in_view(point, 0.25, -100.0f, 0.0f));
	ScreenPoint s = view.to_screen(point);
	CHECK_NEAR(s.x, 800.0 * 0.75 - 100.0, 0.01);
	// Off the pane altogether it goes where the middle of the allowed area is.
	view = make_view();
	MapPoint far_away = view.to_world(5000.0f, 300.0f);
	CHECK(view.keep_in_view(far_away, 0.25, -60.0f, 20.0f));
	ScreenPoint c = view.to_screen(far_away);
	CHECK_NEAR(c.x, 400.0 - 60.0, 0.01);
	CHECK_NEAR(c.y, 300.0 + 20.0, 0.01);
}

static void test_heading() {
	// Flying east, then turning to go north (north is up the screen), 15 meters or so a second at 10 samples a second.
	MapViewport view = make_view();
	const double per_sample = 1.5 / 40075016.686;   // World units: 1.5 meters, near enough.
	MapTrace trace;
	MapPoint at = view.center();
	for (int i = 0; i < 20; i++) {
		trace.add(atan(sinh(3.14159265358979323846 * (1.0 - 2.0 * at.y))) * 180.0 / 3.14159265358979323846, at.x * 360.0 - 180.0);
		at.x += per_sample * (i < 10 ? 1.0 : 0.0);
		at.y -= per_sample * (i < 10 ? 0.0 : 1.0);
	}
	float heading = 0.0f;
	CHECK(trace.heading_at(5.0, &heading));
	CHECK_NEAR(heading, 90.0, 0.5);
	CHECK(trace.heading_at(15.0, &heading));
	CHECK_NEAR(heading, 0.0, 0.5);
	// At the corner it is in between (centered on the sample, so exactly halfway round).
	CHECK(trace.heading_at(10.0, &heading));
	CHECK_NEAR(heading, 45.0, 1.0);
	// In between samples too, and at the very ends it uses what there is on one side.
	CHECK(trace.heading_at(5.5, &heading));
	CHECK_NEAR(heading, 90.0, 0.5);
	CHECK(trace.heading_at(0.0, &heading));
	CHECK_NEAR(heading, 90.0, 0.5);
	// Well outside the recording there is nothing to go on.
	CHECK(!trace.heading_at(-3.0, &heading));

	// Standing still, or with no path, there is no heading.
	MapTrace still;
	for (int i = 0; i < 10; i++)
		still.add(37.7, -122.5);
	CHECK(!still.heading_at(5.0, &heading));
	MapTrace none;
	CHECK(!none.heading_at(0.0, &heading));
	// Heading west is 270.
	MapTrace west;
	for (int i = 0; i < 6; i++)
		west.add(37.7, -122.5 - 0.00002 * i);
	CHECK(west.heading_at(3.0, &heading));
	CHECK_NEAR(heading, 270.0, 0.5);
}

static void test_climb_colors() {
	// The same scale as the video map: the ends are far apart in color, and level flight is yellowish.
	MapColor sink = map_colors::step_color(map_colors::climb_step(-5000.0f, 800.0f), 0.0f);
	MapColor climb = map_colors::step_color(map_colors::climb_step(5000.0f, 800.0f), 0.0f);
	MapColor level = map_colors::step_color(map_colors::climb_step(0.0f, 800.0f), 0.0f);
	CHECK(map_colors::climb_step(-5000.0f, 800.0f) == 0);
	CHECK(map_colors::climb_step(5000.0f, 800.0f) == map_colors::CLIMB_STEPS - 1);
	CHECK(sink.b > sink.r && sink.b > 0.15f);
	CHECK(climb.r > 0.9f && climb.b < 0.2f);
	CHECK(level.r > 0.6f && level.g > 0.6f);
	// Steps in order, one step apart for a small change, the same step for a tiny one.
	CHECK(map_colors::climb_step(0.0f, 800.0f) == map_colors::climb_step(1.0f, 800.0f));
	CHECK(map_colors::climb_step(-800.0f, 800.0f) == 0);
	CHECK(map_colors::climb_step(799.0f, 800.0f) >= map_colors::CLIMB_STEPS - 1);
	// Garbage in, the middle out.
	CHECK(map_colors::climb_step(nanf(""), 800.0f) == map_colors::climb_step(0.0f, 800.0f));
	// The floor on brightness lifts the dark end and leaves the bright part alone.
	MapColor lifted = map_colors::step_color(0, 0.4f);
	CHECK(map_colors::brightness(lifted) >= 0.399f);
	CHECK(map_colors::brightness(sink) < 0.2f);
	MapColor same = map_colors::step_color(map_colors::climb_step(0.0f, 800.0f), 0.4f);
	CHECK_NEAR(same.r, level.r, 1e-6);
	CHECK_NEAR(same.b, level.b, 1e-6);
	for (int step = 0; step < map_colors::CLIMB_STEPS; step++) {
		MapColor c = map_colors::step_color(step, 0.4f);
		CHECK(c.r >= 0.0f && c.r <= 1.0f && c.g >= 0.0f && c.g <= 1.0f && c.b >= 0.0f && c.b <= 1.0f);
		CHECK(map_colors::brightness(c) >= 0.399f);
	}
}

int main() {
	test_projection();
	test_screen_mapping();
	test_zoom_and_pan();
	test_fit();
	test_keep_in_view();
	test_in_inset();
	test_trace_positions();
	test_polyline();
	test_polyline_culling();
	test_curves();
	test_keep_in_view_with_bias();
	test_heading();
	test_climb_colors();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
