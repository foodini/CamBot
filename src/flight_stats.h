#pragma once

// The end-of-video flight summary: statistics over the telemetry that was rendered into the
// video (the same window as the altitude graph). Computed in hindsight over the whole window --
// unlike the climb filters and the map zoom, nothing here is meant to be what an instrument could
// have known at the time. Header-only and free of any dependence on the rest of CamBot so it can
// be tested on its own.
//
// Altitudes are meters, climb rates ft/min (what TelemetrySlice stores), speeds knots, distances
// km, and the results are in the units the summary is displayed in (feet, miles, mph).

#include <math.h>
#include <stdint.h>
#include <algorithm>
#include <vector>

// One telemetry sample, reduced to what the summary needs.
struct FlightSample {
	float lat_deg;
	float lon_deg;
	float alt_m;
	float climb_fpm;
	float speed_kts;
	float course_deg;
	float total_distance_km;   // Cumulative path length, as TelemetrySlice::m_total_distance.
};

struct FlightSummary {
	FlightSummary() :
		valid(false), min_alt_m(0), max_alt_m(0), launch_alt_m(0), max_height_above_launch_m(0),
		max_climb_fpm(0), max_sink_fpm(0), max_speed_mph(0), distance_flown_mi(0),
		distance_covered_mi(0), has_glide(false), best_glide_ratio(0), circle_count(0), circling_seconds(0)
	{}

	bool  valid;
	float min_alt_m;
	float max_alt_m;
	float launch_alt_m;
	float max_height_above_launch_m;
	float max_climb_fpm;             // Peak of the (filtered) climb rate; positive.
	float max_sink_fpm;              // Peak sink, as a negative number.
	float max_speed_mph;
	float distance_flown_mi;         // Path flown, launch to the end of the window.

	// Cross-country statistics.
	float distance_covered_mi;       // Straight line, launch to the end of the window.
	bool  has_glide;                 // False if there was no qualifying stretch of descent.
	float best_glide_ratio;          // Ground distance : altitude lost, over the best stretch.
	int   circle_count;              // Complete circles.
	float circling_seconds;
};

namespace flight_stats {

// Tunables. The glide and circle ones are a first guess, to be tuned against real thermaling.
static const float KM_TO_MILES = 0.621371f;
static const float KNOTS_TO_MPH = 1.15078f;
static const float GLIDE_WINDOW_SECONDS = 60.0f;      // A glide is measured over this long a stretch...
static const float GLIDE_MIN_LOSS_M = 30.48f;         // ...that lost at least 100 ft...
static const float GLIDE_MAX_REGAIN_M = 5.0f;         // ...without climbing more than this along the way.
static const float TURN_MIN_SPEED_KTS = 3.0f;         // GPS course means nothing when barely moving.
static const float TURN_SMOOTHING_SECONDS = 3.0f;     // Turn rate is measured across this centered span.
static const float TURN_MIN_RATE_DPS = 6.0f;          // Turning faster than this (a circle in under a minute)...
static const float TURN_BRIDGE_SECONDS = 4.0f;        // ...with gaps of up to this long bridged...
static const float CIRCLE_DEGREES = 360.0f;           // ...counts as circling; each full 360 is a circle.

inline bool alt_valid(float alt_m) { return alt_m > -9000.0f && alt_m < 90000.0f; }

inline float wrap_degrees(float delta) {
	while (delta > 180.0f)
		delta -= 360.0f;
	while (delta <= -180.0f)
		delta += 360.0f;
	return delta;
}

// Great-circle distance in meters (haversine).
inline float distance_m(const FlightSample& a, const FlightSample& b) {
	const double kDegToRad = 3.14159265358979323846 / 180.0;
	const double lat1 = a.lat_deg * kDegToRad, lat2 = b.lat_deg * kDegToRad;
	const double dlat = lat2 - lat1;
	const double dlon = (b.lon_deg - a.lon_deg) * kDegToRad;
	const double h = sin(dlat / 2) * sin(dlat / 2) + cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
	return (float)(2.0 * 6371000.0 * asin((std::min)(1.0, sqrt(h))));
}

inline bool has_fix(const FlightSample& s) { return !(s.lat_deg == 0.0f && s.lon_deg == 0.0f); }

// Best ground glide ratio over any glide_window-long stretch (from launch on) that lost at least
// GLIDE_MIN_LOSS_M and never climbed more than GLIDE_MAX_REGAIN_M above where it started --
// straight-line ground distance over altitude lost, so turns and wind both count against it.
inline bool best_glide(const std::vector<FlightSample>& s, int launch, float hz, float* ratio_out) {
	const int n = (int)s.size();
	const int span = (std::max)(1, (int)(GLIDE_WINDOW_SECONDS * hz + 0.5f));
	bool found = false;
	float best = 0.0f;
	for (int i = (std::max)(0, launch); i + span < n; i++) {
		if (!alt_valid(s[i].alt_m) || !alt_valid(s[i + span].alt_m))
			continue;
		const float lost = s[i].alt_m - s[i + span].alt_m;
		if (lost < GLIDE_MIN_LOSS_M)
			continue;
		bool climbed = false;
		for (int j = i + 1; j < i + span; j++) {
			if (alt_valid(s[j].alt_m) && s[j].alt_m - s[i].alt_m > GLIDE_MAX_REGAIN_M) {
				climbed = true;
				break;
			}
		}
		if (climbed || !has_fix(s[i]) || !has_fix(s[i + span]))
			continue;
		const float ratio = distance_m(s[i], s[i + span]) / lost;
		if (!found || ratio > best) {
			best = ratio;
			found = true;
		}
	}
	*ratio_out = best;
	return found;
}

// Counts complete circles, and the time spent circling, from launch on. A circling run is a stretch
// of turning faster than TURN_MIN_RATE_DPS in one direction (short pauses bridged); it counts if it
// adds up to at least a full circle, so the half-turns at the ends of a ridge never do. The time is
// a little generous -- the rate is measured across a centered 3 s span, so a run seems to begin
// and end about a second and a half early/late.
inline void count_circles(const std::vector<FlightSample>& s, int launch, float hz, int* circles_out, float* seconds_out) {
	const int n = (int)s.size();
	*circles_out = 0;
	*seconds_out = 0.0f;
	if (n < 3)
		return;
	const int half = (std::max)(1, (int)(TURN_SMOOTHING_SECONDS * hz / 2.0f + 0.5f));
	const int bridge = (int)(TURN_BRIDGE_SECONDS * hz + 0.5f);

	// Unwrapped heading change per sample step (0 when the GPS course can't be trusted).
	std::vector<float> step(n, 0.0f);
	for (int i = 1; i < n; i++) {
		if (s[i].speed_kts >= TURN_MIN_SPEED_KTS && s[i - 1].speed_kts >= TURN_MIN_SPEED_KTS)
			step[i] = wrap_degrees(s[i].course_deg - s[i - 1].course_deg);
	}
	// Centered turn rate in degrees/second.
	std::vector<float> rate(n, 0.0f);
	for (int i = 0; i < n; i++) {
		const int lo = (std::max)(0, i - half);
		const int hi = (std::min)(n - 1, i + half);
		if (hi <= lo || s[lo].speed_kts < TURN_MIN_SPEED_KTS || s[hi].speed_kts < TURN_MIN_SPEED_KTS)
			continue;
		float sum = 0.0f;
		for (int k = lo + 1; k <= hi; k++)
			sum += step[k];
		rate[i] = sum / ((hi - lo) / hz);
	}

	bool active = false;
	int run_start = 0, last_turning = 0, run_sign = 0;
	float accum = 0.0f, accum_at_last = 0.0f;
	auto finalize = [&]() {
		const float total = fabsf(accum_at_last);
		if (total >= CIRCLE_DEGREES) {
			*circles_out += (int)(total / CIRCLE_DEGREES);
			*seconds_out += (last_turning - run_start) / hz;
		}
		active = false;
	};
	for (int i = (std::max)(0, launch); i < n; i++) {
		const bool turning = fabsf(rate[i]) >= TURN_MIN_RATE_DPS;
		const int sign = rate[i] > 0 ? 1 : -1;
		if (active) {
			if (turning && sign != run_sign) {
				finalize();
			} else if (!turning && i - last_turning > bridge) {
				finalize();
			}
		}
		if (!active && turning) {
			active = true;
			run_start = last_turning = i;
			run_sign = sign;
			accum = accum_at_last = 0.0f;
		}
		if (active) {
			accum += step[i];
			if (turning) {
				last_turning = i;
				accum_at_last = accum;
			}
		}
	}
	if (active)
		finalize();
}

// samples: the telemetry window, in order. launch: the index within it where the launch marker is.
inline FlightSummary compute(const std::vector<FlightSample>& s, int launch, float hz, bool include_xc) {
	FlightSummary out;
	const int n = (int)s.size();
	if (n == 0)
		return out;
	launch = (std::max)(0, (std::min)(launch, n - 1));
	out.valid = true;

	bool have_alt = false;
	float min_alt = 0.0f, max_alt = 0.0f;
	float max_climb = -1e9f, max_sink = 1e9f, max_speed = 0.0f;
	for (int i = 0; i < n; i++) {
		if (alt_valid(s[i].alt_m)) {
			if (!have_alt) {
				min_alt = max_alt = s[i].alt_m;
				have_alt = true;
			}
			min_alt = (std::min)(min_alt, s[i].alt_m);
			max_alt = (std::max)(max_alt, s[i].alt_m);
		}
		max_climb = (std::max)(max_climb, s[i].climb_fpm);
		max_sink = (std::min)(max_sink, s[i].climb_fpm);
		max_speed = (std::max)(max_speed, s[i].speed_kts);
	}
	out.min_alt_m = min_alt;
	out.max_alt_m = max_alt;
	out.max_climb_fpm = max_climb;
	out.max_sink_fpm = max_sink;
	out.max_speed_mph = max_speed * KNOTS_TO_MPH;

	// Launch altitude: a short average (+/- 1 s) so one noisy sample doesn't set the baseline.
	{
		const int radius = (int)hz;
		double sum = 0.0;
		int count = 0;
		for (int i = (std::max)(0, launch - radius); i <= (std::min)(n - 1, launch + radius); i++) {
			if (alt_valid(s[i].alt_m)) {
				sum += s[i].alt_m;
				count++;
			}
		}
		out.launch_alt_m = count > 0 ? (float)(sum / count) : min_alt;
		float highest_after_launch = out.launch_alt_m;
		for (int i = launch; i < n; i++) {
			if (alt_valid(s[i].alt_m))
				highest_after_launch = (std::max)(highest_after_launch, s[i].alt_m);
		}
		out.max_height_above_launch_m = highest_after_launch - out.launch_alt_m;
	}

	out.distance_flown_mi = (std::max)(0.0f, s[n - 1].total_distance_km - s[launch].total_distance_km) * KM_TO_MILES;

	if (include_xc) {
		if (has_fix(s[launch]) && has_fix(s[n - 1]))
			out.distance_covered_mi = distance_m(s[launch], s[n - 1]) / 1000.0f * KM_TO_MILES;
		out.has_glide = best_glide(s, launch, hz, &out.best_glide_ratio);
		count_circles(s, launch, hz, &out.circle_count, &out.circling_seconds);
	}
	return out;
}

}  // namespace flight_stats
