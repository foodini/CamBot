#include <math.h>
#include <stdio.h>

#include "tracking_path.h"

using namespace tracking;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); g_failures++; } } while (0)

static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }
static const double PI = 3.14159265358979323846;
static double radians(double degrees) { return degrees * PI / 180.0; }
static const double EXACT = 1e-9;

static bool near_vec(const Vec3& a, const Vec3& b, double tolerance) {
	return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// A camera turning steadily to the right: `degrees` of yaw at tick 100, in a frame every 10 ticks.
static CameraOrientations steady_yaw(double degrees) {
	CameraOrientations orientations;
	for (int64_t t = 0; t <= 100; t += 10)
		orientations.add(t, from_axis_angle(Vec3(0, 1, 0), radians(degrees * (double)t / 100.0)));
	return orientations;
}

static void test_rotations() {
	const Quat quarter = from_axis_angle(Vec3(0, 1, 0), radians(90.0));
	CHECK(near_vec(rotate(quarter, Vec3(0, 0, 1)), Vec3(1, 0, 0), EXACT));
	CHECK(near_vec(rotate(Quat(), Vec3(0.3, -0.5, 0.8)), Vec3(0.3, -0.5, 0.8), EXACT));
	const Quat other = from_axis_angle(Vec3(1, 1, 0), radians(37.0));
	const Vec3 v(0.2, 0.7, -0.4);
	CHECK(near_vec(rotate(conjugate(other), rotate(other, v)), v, EXACT));
	CHECK(near_vec(rotate(multiply(quarter, other), v), rotate(quarter, rotate(other, v)), EXACT));
	CHECK(near(length(rotate(other, v)), length(v), EXACT));
	// Halfway between no turn and 60 degrees is 30.
	const Quat half = slerp(Quat(), from_axis_angle(Vec3(0, 1, 0), radians(60.0)), 0.5);
	CHECK(near_vec(rotate(half, Vec3(0, 0, 1)), rotate(from_axis_angle(Vec3(0, 1, 0), radians(30.0)), Vec3(0, 0, 1)), EXACT));
	// The same turn written as the negative quaternion is not a long way round.
	const Quat negative(-quarter.w, -quarter.x, -quarter.y, -quarter.z);
	const Quat mid = slerp(Quat(), negative, 0.5);
	CHECK(near_vec(rotate(mid, Vec3(0, 0, 1)), rotate(from_axis_angle(Vec3(0, 1, 0), radians(45.0)), Vec3(0, 0, 1)), EXACT));
	CHECK(near(length(normalized(Vec3(3, 4, 0))), 1.0, EXACT));
}

static void test_lens() {
	const CameraLens lens(radians(90.0), 16.0 / 9.0);
	CHECK(near_vec(lens.ray_for(0.5, 0.5), Vec3(0, 0, 1), EXACT));
	// The right-hand edge is half the field of view off the lens axis.
	CHECK(near(dot(lens.ray_for(1.0, 0.5), Vec3(0, 0, 1)), cos(radians(45.0)), EXACT));
	CHECK(lens.ray_for(1.0, 0.5).x > 0.0 && lens.ray_for(0.5, 1.0).y > 0.0);
	const double places[][2] = { { 0.5, 0.5 }, { 0.1, 0.9 }, { 0.95, 0.05 }, { 0.0, 0.0 }, { 1.0, 1.0 } };
	for (int i = 0; i < 5; i++) {
		double u = -1.0;
		double v = -1.0;
		CHECK(lens.project(lens.ray_for(places[i][0], places[i][1]), &u, &v));
		CHECK(near(u, places[i][0], EXACT) && near(v, places[i][1], EXACT));
	}
	double u = 7.0;
	double v = 7.0;
	CHECK(!lens.project(Vec3(0, 0, -1), &u, &v));
	CHECK(!lens.project(Vec3(1, 0, 0), &u, &v));
	CHECK(u == 7.0 && v == 7.0);
	// In front of the camera but outside the picture is still a place.
	CHECK(lens.project(Vec3(2, 0, 1), &u, &v) && u > 1.0);
}

static void test_orientations() {
	CameraOrientations orientations;
	Quat q;
	CHECK(orientations.empty() && !orientations.at(5, &q));
	orientations.add(100, from_axis_angle(Vec3(0, 1, 0), radians(60.0)));
	orientations.add(0, Quat());
	CHECK(!orientations.empty());
	CHECK(orientations.at(50, &q));
	CHECK(near_vec(rotate(q, Vec3(0, 0, 1)), rotate(from_axis_angle(Vec3(0, 1, 0), radians(30.0)), Vec3(0, 0, 1)), EXACT));
	// Before the first and after the last, the nearest.
	CHECK(orientations.at(-50, &q) && near_vec(rotate(q, Vec3(0, 0, 1)), Vec3(0, 0, 1), EXACT));
	CHECK(orientations.at(500, &q) && near_vec(rotate(q, Vec3(0, 0, 1)), rotate(from_axis_angle(Vec3(0, 1, 0), radians(60.0)), Vec3(0, 0, 1)), EXACT));
	// A time already there is replaced.
	orientations.add(100, Quat());
	CHECK(orientations.at(100, &q) && near_vec(rotate(q, Vec3(0, 0, 1)), Vec3(0, 0, 1), EXACT));
	orientations.clear();
	CHECK(orientations.empty());
}

static void test_points() {
	TrackingPath path;
	CHECK(path.empty());
	path.set_point(300, 0.3, 0.3);
	path.set_point(100, 0.1, 0.1);
	path.set_point(200, 0.2, 0.2);
	CHECK(path.points().size() == 3);
	CHECK(path.first_ticks() == 100 && path.last_ticks() == 300);
	CHECK(path.points()[1].ticks == 200);
	// One point to a frame: the same frame moves it.
	path.set_point(200, 0.9, 0.8);
	CHECK(path.points().size() == 3 && near(path.points()[1].u, 0.9, EXACT) && near(path.points()[1].v, 0.8, EXACT));
	CHECK(!path.remove_point(250));
	CHECK(path.remove_point(200));
	CHECK(path.points().size() == 2 && path.points()[1].ticks == 300);
	CHECK(path.orientation_aware());
	path.set_orientation_aware(false);
	CHECK(!path.orientation_aware());
}

static void test_screen_curve() {
	const CameraLens lens(radians(90.0), 16.0 / 9.0);
	const CameraOrientations none;
	TrackingPath path;
	path.set_orientation_aware(false);
	double u = 0.0;
	double v = 0.0;
	CHECK(!path.position_at(0, lens, none, &u, &v));       // Empty.
	// One point is a place that stays.
	path.set_point(1000, 0.4, 0.6);
	CHECK(path.position_at(-5, lens, none, &u, &v) && near(u, 0.4, EXACT) && near(v, 0.6, EXACT));
	CHECK(path.position_at(99999, lens, none, &u, &v) && near(u, 0.4, EXACT) && near(v, 0.6, EXACT));
	// Two points are a straight line, at an even pace.
	path.set_point(2000, 0.8, 0.2);
	CHECK(path.position_at(1500, lens, none, &u, &v) && near(u, 0.6, EXACT) && near(v, 0.4, EXACT));
	// Before the first and after the last, it waits at the end.
	CHECK(path.position_at(0, lens, none, &u, &v) && near(u, 0.4, EXACT) && near(v, 0.6, EXACT));
	CHECK(path.position_at(5000, lens, none, &u, &v) && near(u, 0.8, EXACT) && near(v, 0.2, EXACT));

	// Through every point exactly, however unevenly they are spaced.
	TrackingPath bent;
	bent.set_orientation_aware(false);
	const int64_t times[] = { 0, 40000, 400000, 410000, 2000000 };
	const double us[] = { 0.1, 0.5, 0.3, 0.9, 0.2 };
	for (int i = 0; i < 5; i++)
		bent.set_point(times[i], us[i], 0.5 + 0.1 * i);
	for (int i = 0; i < 5; i++) {
		CHECK(bent.position_at(times[i], lens, none, &u, &v));
		CHECK(near(u, us[i], EXACT) && near(v, 0.5 + 0.1 * i, EXACT));
	}
	// And smooth through them: the slope is the same just before and just after a point (a kink would be a jump of the size
	// of the slope itself, some thousand times what the curve's own bend makes in one tick).
	for (int i = 1; i < 4; i++) {
		double before, at, after, ignored;
		bent.position_at(times[i] - 1, lens, none, &before, &ignored);
		bent.position_at(times[i], lens, none, &at, &ignored);
		bent.position_at(times[i] + 1, lens, none, &after, &ignored);
		CHECK(near(at - before, after - at, 1e-7));
	}

	// It never goes beyond the points either side: a level stretch that then rises stays level, and nothing leaves 0 to 1.
	TrackingPath level;
	level.set_orientation_aware(false);
	level.set_point(0, 0.0, 0.5);
	level.set_point(10, 0.0, 0.5);
	level.set_point(20, 1.0, 0.5);
	double last = -1.0;
	for (int64_t t = 0; t <= 20; t++) {
		level.position_at(t, lens, none, &u, &v);
		CHECK(u >= -1e-12 && u <= 1.0 + 1e-12);
		CHECK(u >= last - 1e-12);
		last = u;
	}
	CHECK(level.position_at(5, lens, none, &u, &v) && near(u, 0.0, 1e-12));
}

static void test_a_target_that_holds_its_bearing() {
	// The camera turns 40 degrees to the right over the clip. Something that keeps its place in the world slides left across
	// the picture, so the two points the user clicked on it, at the start and the end, are where it really was then.
	const CameraLens lens(radians(90.0), 16.0 / 9.0);     // Half the width is one unit out, so u = 0.5 - tan(yaw) / 2.
	const CameraOrientations orientations = steady_yaw(40.0);
	const double end_u = 0.5 - tan(radians(40.0)) / 2.0;
	TrackingPath path;
	path.set_point(0, 0.5, 0.5);
	path.set_point(100, end_u, 0.5);
	double u = 0.0;
	double v = 0.0;
	for (int64_t t = 0; t <= 100; t += 10) {
		CHECK(path.position_at(t, lens, orientations, &u, &v));
		CHECK(near(u, 0.5 - tan(radians(40.0 * (double)t / 100.0)) / 2.0, 1e-9));
		CHECK(near(v, 0.5, 1e-9));
	}
	// Between frames too.
	CHECK(path.position_at(55, lens, orientations, &u, &v));
	CHECK(near(u, 0.5 - tan(radians(22.0)) / 2.0, 1e-3));
	// Straight interpolation of the two places knows nothing of the turning and is a long way off.
	path.set_orientation_aware(false);
	CHECK(path.position_at(50, lens, orientations, &u, &v));
	CHECK(fabs(u - (0.5 - tan(radians(20.0)) / 2.0)) > 0.01);
	// Aware, but with no orientation to go on: the same as unticked.
	path.set_orientation_aware(true);
	double unaware_u;
	path.set_orientation_aware(false);
	path.position_at(50, lens, CameraOrientations(), &unaware_u, &v);
	path.set_orientation_aware(true);
	CHECK(path.position_at(50, lens, CameraOrientations(), &u, &v) && near(u, unaware_u, EXACT));
}

static void test_a_target_that_moves() {
	// A target that drifts through the world as the camera turns: whatever the interpolation does between, the path is exactly
	// where it was put at each point, and both the camera's turn and the target's are in the curve.
	const CameraLens lens(radians(100.0), 16.0 / 9.0);
	CameraOrientations orientations;
	for (int64_t t = 0; t <= 300; t += 10) {
		const Quat yaw = from_axis_angle(Vec3(0, 1, 0), radians(0.2 * (double)t));
		const Quat pitch = from_axis_angle(Vec3(1, 0, 0), radians(10.0 * sin((double)t / 60.0)));
		orientations.add(t, multiply(yaw, pitch));
	}
	TrackingPath path;
	path.set_point(0, 0.5, 0.5);
	path.set_point(100, 0.3, 0.4);
	path.set_point(220, 0.7, 0.6);
	path.set_point(300, 0.5, 0.3);
	double u = 0.0;
	double v = 0.0;
	for (size_t i = 0; i < path.points().size(); i++) {
		CHECK(path.position_at(path.points()[i].ticks, lens, orientations, &u, &v));
		CHECK(near(u, path.points()[i].u, 1e-9) && near(v, path.points()[i].v, 1e-9));
	}
	// Between points it moves smoothly: no jump between neighboring ticks.
	double last_u = 0.5;
	double last_v = 0.5;
	for (int64_t t = 1; t <= 300; t++) {
		CHECK(path.position_at(t, lens, orientations, &u, &v));
		CHECK(fabs(u - last_u) < 0.05 && fabs(v - last_v) < 0.05);
		last_u = u;
		last_v = v;
	}
}

static void test_behind_the_camera() {
	const CameraLens lens(radians(90.0), 16.0 / 9.0);
	CameraOrientations orientations;
	orientations.add(0, Quat());
	orientations.add(50, from_axis_angle(Vec3(0, 1, 0), radians(170.0)));
	orientations.add(100, Quat());
	TrackingPath path;
	path.set_point(0, 0.5, 0.5);
	path.set_point(100, 0.5, 0.5);
	double u = 7.0;
	double v = 7.0;
	CHECK(!path.position_at(50, lens, orientations, &u, &v));
	CHECK(u == 7.0 && v == 7.0);
	CHECK(path.position_at(10, lens, orientations, &u, &v));
}

static void test_trimming() {
	const CameraLens lens(radians(90.0), 16.0 / 9.0);
	const CameraOrientations none;
	TrackingPath path;
	path.set_orientation_aware(false);
	path.set_point(0, 0.1, 0.1);
	path.set_point(100, 0.5, 0.5);
	path.set_point(200, 0.9, 0.3);
	double before_u, before_v, u, v;
	path.position_at(150, lens, none, &before_u, &before_v);

	TrackingPath left = path;
	CHECK(left.trim_left(150, lens, none));
	CHECK(left.first_ticks() == 150 && left.last_ticks() == 200 && left.points().size() == 2);
	CHECK(near(left.points()[0].u, before_u, EXACT) && near(left.points()[0].v, before_v, EXACT));
	CHECK(left.position_at(200, lens, none, &u, &v) && near(u, 0.9, EXACT));

	TrackingPath right = path;
	CHECK(right.trim_right(150, lens, none));
	CHECK(right.first_ticks() == 0 && right.last_ticks() == 150 && right.points().size() == 3);
	CHECK(near(right.points()[2].u, before_u, EXACT) && near(right.points()[2].v, before_v, EXACT));
	CHECK(right.position_at(100, lens, none, &u, &v) && near(u, 0.5, EXACT));

	// At a point that is already there, nothing is added.
	TrackingPath at_point = path;
	CHECK(at_point.trim_left(100, lens, none));
	CHECK(at_point.points().size() == 2 && at_point.first_ticks() == 100 && near(at_point.points()[0].u, 0.5, EXACT));
	at_point = path;
	CHECK(at_point.trim_right(100, lens, none));
	CHECK(at_point.points().size() == 2 && at_point.last_ticks() == 100);

	// Nothing to cut at or beyond the end being trimmed; refused when it would leave nothing.
	TrackingPath same = path;
	CHECK(same.trim_left(0, lens, none) && same.trim_left(-10, lens, none) && same.points().size() == 3);
	CHECK(same.trim_right(200, lens, none) && same.trim_right(900, lens, none) && same.points().size() == 3);
	CHECK(!same.trim_left(201, lens, none) && same.points().size() == 3);
	CHECK(!same.trim_right(-1, lens, none) && same.points().size() == 3);
	TrackingPath empty;
	CHECK(!empty.trim_left(0, lens, none) && !empty.trim_right(0, lens, none));
}

int main() {
	test_rotations();
	test_lens();
	test_orientations();
	test_points();
	test_screen_curve();
	test_a_target_that_holds_its_bearing();
	test_a_target_that_moves();
	test_behind_the_camera();
	test_trimming();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
