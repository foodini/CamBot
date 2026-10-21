#pragma once

// Causal map zoom: how many meters wide the map should be at each telemetry sample.
//
// In straight flight the map shows its widest view. Once the glider has turned through a full
// circle within the last 45 s, the map zooms in to fit the last stretch of path around the
// pilot; once it has stopped turning, it zooms back out. Everything is computed from the
// current sample and earlier ones only, so the zoom at any moment is a function of what has
// already happened -- which is also what makes it safe to look up by sample index while
// scrubbing, seeking, or exporting.
//
// No GL or CamBot dependencies, so it can be tested on its own.

#include <math.h>
#include <algorithm>
#include <vector>

struct MapZoomSettings {
	MapZoomSettings() : min_width_km(0.2f), max_width_km(2.0f), transition_seconds(12.0f) {}

	float min_width_km;        // Never zoom in tighter than this (map width).
	float max_width_km;        // Never zoom out wider than this; also the straight-flight view.
	float transition_seconds;  // Roughly how long a zoom-in or zoom-out takes.
};

class MapZoomTracker {
public:
	MapZoomTracker(const MapZoomSettings& settings, float sample_hz) :
		m_min_width_m(settings.min_width_km * 1000.0f),
		m_max_width_m((std::max)(settings.min_width_km, settings.max_width_km) * 1000.0f),
		m_dt(1.0f / sample_hz),
		m_enter_samples((int)(ENTER_WINDOW_SECONDS * sample_hz + 0.5f)),
		m_exit_samples((int)(EXIT_WINDOW_SECONDS * sample_hz + 0.5f)),
		m_cumulative_turn_deg(0.0f),
		m_last_course_deg(0.0f),
		m_have_course(false),
		m_circling(false),
		m_log_width(0.0f),
		m_initialized(false)
	{
		// Reaches ~95% of the way to a new target after transition_seconds (three time constants).
		float tau = (std::max)(settings.transition_seconds / 3.0f, m_dt);
		m_smoothing = 1.0f - expf(-m_dt / tau);
	}

	// Feed one sample per call, in order. Returns the map width in meters for that sample.
	float update(float lat_deg, float lon_deg, float speed_kts, float course_deg) {
		const int i = (int)m_lat.size();
		m_lat.push_back(lat_deg);
		m_lon.push_back(lon_deg);

		// Unwrapped, signed total turn so far. GPS course is meaningless when barely moving, so
		// it's ignored below MIN_TURN_SPEED_KTS.
		if (speed_kts >= MIN_TURN_SPEED_KTS) {
			if (m_have_course) {
				float delta = course_deg - m_last_course_deg;
				while (delta > 180.0f)
					delta -= 360.0f;
				while (delta <= -180.0f)
					delta += 360.0f;
				m_cumulative_turn_deg += delta;
			}
			m_last_course_deg = course_deg;
			m_have_course = true;
		}
		m_turn.push_back(m_cumulative_turn_deg);

		// Net turn over the last 45 s (enter) and 10 s (exit). Net, not total, so S-turns that
		// undo each other don't count as circling.
		const float turn_enter = fabsf(m_cumulative_turn_deg - m_turn[(std::max)(0, i - m_enter_samples)]);
		const float turn_exit = fabsf(m_cumulative_turn_deg - m_turn[(std::max)(0, i - m_exit_samples)]);
		// Entering also requires that it's still turning (turn_exit): the full circle stays in the
		// 45 s window for a long time after a thermal is left, and without this check it would flip
		// straight back to "circling" the moment it ended.
		if (!m_circling) {
			if (turn_enter >= ENTER_TURN_DEG && turn_exit >= EXIT_TURN_DEG)
				m_circling = true;
		} else if (turn_exit < EXIT_TURN_DEG) {
			m_circling = false;
		}

		float target_m = m_max_width_m;
		if (m_circling) {
			// Fit the path of the last 45 s. The map is centered on the pilot, who is somewhere on
			// that path, so the map has to be wide enough to reach the far side of it from any
			// point on it -- i.e. as wide as the path's overall extent. The diagonal of its bounding
			// box is a stable stand-in for that; sizing to the distance from the pilot to the
			// farthest point instead would swell and shrink once per circle as the pilot goes around.
			const float meters_per_deg_lat = 1.0e7f / 90.0f;
			const float meters_per_deg_lon = meters_per_deg_lat * cosf(lat_deg * 3.14159265f / 180.0f);
			float min_x = 0.0f, max_x = 0.0f, min_y = 0.0f, max_y = 0.0f;
			for (int j = (std::max)(0, i - m_enter_samples); j < i; j++) {
				const float x = (m_lon[j] - lon_deg) * meters_per_deg_lon;
				const float y = (m_lat[j] - lat_deg) * meters_per_deg_lat;
				min_x = (std::min)(min_x, x);
				max_x = (std::max)(max_x, x);
				min_y = (std::min)(min_y, y);
				max_y = (std::max)(max_y, y);
			}
			target_m = sqrtf((max_x - min_x) * (max_x - min_x) + (max_y - min_y) * (max_y - min_y)) * FIT_MARGIN;
			target_m = (std::min)((std::max)(target_m, m_min_width_m), m_max_width_m);
		}

		// Ease toward the target in log space, so zooming in and out feel equally fast.
		if (!m_initialized) {
			m_log_width = logf(m_max_width_m);
			m_initialized = true;
		} else {
			m_log_width += m_smoothing * (logf(target_m) - m_log_width);
		}
		return expf(m_log_width);
	}

	bool circling() const { return m_circling; }

	// Circling begins when the net turn over the last ENTER_WINDOW_SECONDS reaches ENTER_TURN_DEG
	// (a full circle), and ends when the net turn over the last EXIT_WINDOW_SECONDS falls below
	// EXIT_TURN_DEG. FIT_MARGIN pads the fitted width so the path isn't on the very edge.
	static constexpr float ENTER_WINDOW_SECONDS = 45.0f;
	static constexpr float ENTER_TURN_DEG = 360.0f;
	static constexpr float EXIT_WINDOW_SECONDS = 10.0f;
	static constexpr float EXIT_TURN_DEG = 90.0f;
	static constexpr float MIN_TURN_SPEED_KTS = 3.0f;
	static constexpr float FIT_MARGIN = 1.1f;

private:
	float m_min_width_m;
	float m_max_width_m;
	float m_dt;
	int   m_enter_samples;
	int   m_exit_samples;
	float m_smoothing;

	std::vector<float> m_lat;
	std::vector<float> m_lon;
	std::vector<float> m_turn;   // cumulative unwrapped turn (degrees) at each sample
	float m_cumulative_turn_deg;
	float m_last_course_deg;
	bool  m_have_course;
	bool  m_circling;
	float m_log_width;
	bool  m_initialized;
};
