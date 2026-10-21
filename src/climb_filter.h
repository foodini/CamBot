#pragma once

// Causal climb-rate (variometer) filters.
//
// Written to be lifted straight into the telemetry collector's firmware: no STL, no heap, no
// dependencies beyond <math.h>/<stdint.h>, fixed-size state, and strictly causal -- the output
// for sample N depends only on samples 0..N, never on anything later. That's the whole point
// of running them in CamBot first: what the overlay shows at any moment is exactly what a
// pilot-facing instrument could have shown at that moment.
//
// Feed each filter one barometric altitude reading (meters) per call, at a fixed sample rate,
// and it returns the smoothed climb rate in meters/second. To lift one into firmware, copy
// WindowedSlopeFilter (or AlphaBetaFilter) and its configure()/reset()/update() trio; the
// ClimbFilterSettings/ClimbFilter wrapper at the bottom is only there so CamBot can switch
// between them at runtime.

#include <math.h>
#include <stdint.h>

// Longest trailing window WindowedSlopeFilter can hold, in samples. 256 samples is 25.6 s at
// CamBot's 10 Hz telemetry.
#define CLIMB_FILTER_MAX_SAMPLES 256

// ---------------------------------------------------------------------------------------------
// Trailing-window least-squares slope.
//
// Fits a straight line to the most recent N altitude samples and reports its slope. Compared
// with a plain two-point difference over the same span it uses every sample in between, so it
// has several times less noise for the same lag. The reported value describes the middle of the
// window, so it trails reality by about half the window length.
//
// Until N samples have arrived it fits whatever it has (two samples minimum, 0 before that).
class WindowedSlopeFilter {
public:
	WindowedSlopeFilter() : m_window(3), m_next(0), m_count(0), m_sample_hz(10.0f) {
		for (int i = 0; i < CLIMB_FILTER_MAX_SAMPLES; i++)
			m_buf[i] = 0.0f;
	}

	// Number of samples in a window spanning window_seconds from first to last sample.
	static int samples_for_window(float window_seconds, float sample_hz) {
		int n = (int)(window_seconds * sample_hz + 0.5f) + 1;
		if (n < 3)
			n = 3;
		if (n > CLIMB_FILTER_MAX_SAMPLES)
			n = CLIMB_FILTER_MAX_SAMPLES;
		return n;
	}

	void configure(float window_seconds, float sample_hz) {
		m_sample_hz = sample_hz;
		m_window = samples_for_window(window_seconds, sample_hz);
		reset();
	}

	void reset() {
		m_next = 0;
		m_count = 0;
	}

	// altitude_m in, climb rate (m/s) out.
	float update(float altitude_m) {
		m_buf[m_next] = altitude_m;
		m_next = (m_next + 1 == m_window) ? 0 : m_next + 1;
		if (m_count < m_window)
			m_count++;
		if (m_count < 2)
			return 0.0f;

		// Slope = sum((k - kbar) * y_k) / sum((k - kbar)^2), k = 0 for the oldest sample. The
		// weights sum to zero, so subtracting any constant from y changes nothing mathematically;
		// subtracting the newest sample keeps the numbers small for float.
		const int n = m_count;
		int idx = m_next - n;
		if (idx < 0)
			idx += m_window;
		const float newest = altitude_m;
		const float center = 0.5f * (float)(n - 1);
		float numerator = 0.0f;
		for (int k = 0; k < n; k++) {
			numerator += ((float)k - center) * (m_buf[idx] - newest);
			idx = (idx + 1 == m_window) ? 0 : idx + 1;
		}
		const float denominator = (float)n * ((float)n * (float)n - 1.0f) / 12.0f;
		return (numerator / denominator) * m_sample_hz;  // per-sample slope -> per-second
	}

private:
	float m_buf[CLIMB_FILTER_MAX_SAMPLES];
	int   m_window;
	int   m_next;
	int   m_count;
	float m_sample_hz;
};

// ---------------------------------------------------------------------------------------------
// Alpha-beta filter, critically damped.
//
// Tracks altitude and climb rate together: each sample, predict where the altitude should be
// given the current rate, then nudge both the altitude and the rate by fixed fractions of the
// prediction error. Two floats of state, no buffer. One knob: a time constant. Both of the
// filter's poles sit at exp(-dt/tau), so it behaves like two first-order lags in series --
// the output trails reality by about 2*tau.
//
// This is the simple cousin of a Kalman filter: if the accelerometer ever gets fused in,
// this structure is what grows into it.
class AlphaBetaFilter {
public:
	AlphaBetaFilter() : m_alpha(0.0f), m_beta(0.0f), m_dt(0.1f), m_origin(0.0f), m_altitude(0.0f), m_rate(0.0f), m_initialized(false) {}

	void configure(float time_constant_seconds, float sample_hz) {
		m_dt = 1.0f / sample_hz;
		float tau = time_constant_seconds;
		if (tau < 0.5f * m_dt)
			tau = 0.5f * m_dt;
		const float theta = expf(-m_dt / tau);
		m_alpha = 1.0f - theta * theta;
		m_beta = (1.0f - theta) * (1.0f - theta);
		reset();
	}

	void reset() {
		m_initialized = false;
		m_origin = 0.0f;
		m_altitude = 0.0f;
		m_rate = 0.0f;
	}

	// altitude_m in, climb rate (m/s) out.
	float update(float altitude_m) {
		if (!m_initialized) {
			// Work in altitude relative to the first reading: a float near 1000 m only resolves
			// ~0.00006 m, which starts to matter once the filter is averaging noise down.
			m_origin = altitude_m;
			m_altitude = 0.0f;
			m_rate = 0.0f;
			m_initialized = true;
			return 0.0f;
		}
		const float predicted = m_altitude + m_rate * m_dt;
		const float residual = (altitude_m - m_origin) - predicted;
		m_altitude = predicted + m_alpha * residual;
		m_rate += (m_beta / m_dt) * residual;
		return m_rate;
	}

private:
	float m_alpha;
	float m_beta;
	float m_dt;
	float m_origin;
	float m_altitude;
	float m_rate;
	bool  m_initialized;
};

// ---------------------------------------------------------------------------------------------
// Runtime-selectable wrapper used by CamBot, plus the rules of thumb its settings window shows.

enum ClimbFilterType {
	CLIMB_FILTER_WINDOWED_SLOPE = 0,
	CLIMB_FILTER_ALPHA_BETA = 1,
	CLIMB_FILTER_TYPE_COUNT
};

struct ClimbFilterSettings {
	ClimbFilterSettings() : type(CLIMB_FILTER_WINDOWED_SLOPE), window_seconds(2.0f), time_constant_seconds(1.0f) {}

	int   type;                    // ClimbFilterType
	float window_seconds;          // used by CLIMB_FILTER_WINDOWED_SLOPE
	float time_constant_seconds;   // used by CLIMB_FILTER_ALPHA_BETA
};

class ClimbFilter {
public:
	void configure(const ClimbFilterSettings& settings, float sample_hz) {
		m_type = settings.type;
		if (m_type == CLIMB_FILTER_ALPHA_BETA)
			m_alpha_beta.configure(settings.time_constant_seconds, sample_hz);
		else
			m_windowed.configure(settings.window_seconds, sample_hz);
	}

	void reset() {
		m_windowed.reset();
		m_alpha_beta.reset();
	}

	// A sensor that isn't there (or has dropped out) reads valid == false: that restarts the
	// filter and reports 0, rather than smoothing toward whatever junk value it logs.
	float update(float altitude_m, bool valid) {
		if (!valid) {
			reset();
			return 0.0f;
		}
		return (m_type == CLIMB_FILTER_ALPHA_BETA) ? m_alpha_beta.update(altitude_m) : m_windowed.update(altitude_m);
	}

private:
	int                 m_type = CLIMB_FILTER_WINDOWED_SLOPE;
	WindowedSlopeFilter m_windowed;
	AlphaBetaFilter     m_alpha_beta;
};

// Standard deviation of the filter's output (m/s) when the altitude readings carry independent
// (white) noise of sensor_sigma_m meters rms. Real baro noise is not perfectly white, so
// treat this as a floor.
inline float climb_filter_expected_noise_mps(int type, float param_seconds, float sensor_sigma_m, float sample_hz) {
	if (type == CLIMB_FILTER_ALPHA_BETA) {
		const float dt = 1.0f / sample_hz;
		return sensor_sigma_m * sqrtf(dt / (4.0f * param_seconds * param_seconds * param_seconds));
	}
	const float n = (float)WindowedSlopeFilter::samples_for_window(param_seconds, sample_hz);
	return sensor_sigma_m * sqrtf(12.0f / (n * (n * n - 1.0f))) * sample_hz;
}

// Approximate delay (seconds) between a real change in climb rate and the filter reporting it.
inline float climb_filter_expected_lag_seconds(int type, float param_seconds, float sample_hz) {
	if (type == CLIMB_FILTER_ALPHA_BETA)
		return 2.0f * param_seconds;
	return 0.5f * (float)(WindowedSlopeFilter::samples_for_window(param_seconds, sample_hz) - 1) / sample_hz;
}
