#include <math.h>
#include <stdio.h>
#include <vector>

#include "position_smoothing.h"

static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double va = (a), vb = (b); if (fabs(va - vb) > (tol)) { printf("FAIL line %d: %s = %.9f, wanted %.9f\n", __LINE__, #a, va, vb); g_failures++; } } while (0)

static void test_fix() {
	CHECK(position_smoothing::has_fix(37.7, -122.5));
	CHECK(!position_smoothing::has_fix(0.0, 0.0));
	CHECK(!position_smoothing::has_fix(95.0, 10.0));
	CHECK(position_smoothing::has_fix(0.0, 10.0));
}

static void test_straight_line_stays() {
	std::vector<double> lat, lon, out_lat, out_lon;
	for (int i = 0; i < 20; i++) {
		lat.push_back(37.0 + 0.00001 * i);
		lon.push_back(-122.0 - 0.00002 * i);
	}
	position_smoothing::smooth(lat, lon, 2, &out_lat, &out_lon);
	CHECK(out_lat.size() == 20 && out_lon.size() == 20);
	for (int i = 0; i < 20; i++) {
		CHECK_NEAR(out_lat[i], lat[i], 1e-12);
		CHECK_NEAR(out_lon[i], lon[i], 1e-12);
	}
}

static void test_ends_stay() {
	std::vector<double> lat, lon, out_lat, out_lon;
	for (int i = 0; i < 10; i++) {
		lat.push_back(37.0 + 0.0001 * (i % 3));
		lon.push_back(-122.0);
	}
	position_smoothing::smooth(lat, lon, 2, &out_lat, &out_lon);
	CHECK(out_lat[0] == lat[0] && out_lat[9] == lat[9]);
	// One in from an end the window is one either side.
	CHECK_NEAR(out_lat[1], (1.0 * lat[0] + 2.0 * lat[1] + 1.0 * lat[2]) / 4.0, 1e-12);
}

static void test_staircase_is_smoothed() {
	// A line going diagonally, rounded down to steps of 0.4 meters or so as the logger does.
	const double step = 3.8147e-6;
	std::vector<double> lat, lon, out_lat, out_lon, true_lat, true_lon;
	for (int i = 0; i < 200; i++) {
		double t_lat = 37.7 + 0.000013 * i;
		double t_lon = -122.5 + 0.000017 * i;
		true_lat.push_back(t_lat);
		true_lon.push_back(t_lon);
		lat.push_back(floor(t_lat / step) * step);
		lon.push_back(floor(t_lon / step) * step);
	}
	position_smoothing::smooth(lat, lon, 2, &out_lat, &out_lon);
	double raw_error = 0.0, smooth_error = 0.0;
	for (int i = 3; i < 197; i++) {
		raw_error += fabs(lat[i] - true_lat[i]) + fabs(lon[i] - true_lon[i]);
		smooth_error += fabs(out_lat[i] - true_lat[i]) + fabs(out_lon[i] - true_lon[i]);
	}
	// (The rounding down leaves half a step of bias in both; the point is how much it wanders.)
	double raw_wander = 0.0, smooth_wander = 0.0;
	for (int i = 3; i < 197; i++) {
		raw_wander += fabs((lat[i] - true_lat[i]) - (lat[i - 1] - true_lat[i - 1]));
		smooth_wander += fabs((out_lat[i] - true_lat[i]) - (out_lat[i - 1] - true_lat[i - 1]));
	}
	CHECK(smooth_wander < raw_wander * 0.5);
	(void)raw_error; (void)smooth_error;
}

static void test_no_fix() {
	std::vector<double> lat, lon, out_lat, out_lon;
	for (int i = 0; i < 9; i++) {
		lat.push_back(37.0 + 0.0001 * i);
		lon.push_back(-122.0);
	}
	lat[4] = 0.0;
	lon[4] = 0.0;
	position_smoothing::smooth(lat, lon, 2, &out_lat, &out_lon);
	// No fix is kept as it was, and isn't averaged into its neighbors.
	CHECK(out_lat[4] == 0.0 && out_lon[4] == 0.0);
	CHECK(out_lat[3] > 36.99 && out_lat[5] > 36.99);
	// All of it with no fix is untouched.
	std::vector<double> zeros(5, 0.0);
	position_smoothing::smooth(zeros, zeros, 2, &out_lat, &out_lon);
	CHECK(out_lat.size() == 5 && out_lat[2] == 0.0);
	// Empty is empty.
	std::vector<double> none;
	position_smoothing::smooth(none, none, 2, &out_lat, &out_lon);
	CHECK(out_lat.empty() && out_lon.empty());
}

int main() {
	test_fix();
	test_straight_line_stays();
	test_ends_stay();
	test_staircase_is_smoothed();
	test_no_fix();
	printf(g_failures ? "FAILED\n" : "all ok\n");
	return g_failures;
}
