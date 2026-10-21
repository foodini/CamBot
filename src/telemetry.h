#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <limits>
#include <utility>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include "altitude_reference.h"
#include "climb_filter.h"
#include "map_zoom.h"

// Forward-declared rather than #include <json/json.h> here, so that header doesn't leak
// into everything that includes telemetry.h -- only telemetry.cpp needs the real definition.
namespace Json { class Value; }

class TelemetryMgr;
class WidgetBase;

struct pair_hash
{
	template <class T1, class T2>
	std::size_t operator() (const std::pair<T1, T2>& pair) const
	{
		return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
	}
};

//TODO(P0): Anything that depends upon this will screw up if telemetry frames were dropped.
//          You should, when parsing the file, fill in any missing data (and warn if too much
//          is gone.)
#define TELEMETRY_FREQUENCY 10

// Climb rates are stored and displayed in feet/minute (what US varios show); the filters in
// climb_filter.h work in meters/second.
#define METERS_PER_SECOND_TO_FEET_PER_MINUTE (60.0f * 3.28084f)

class TelemetrySlice {
public:
	TelemetrySlice();
	// tz_correction is an in/out cache: pass the same time_t, initialized to
	// TelemetrySlice::TZ_CORRECTION_UNCOMPUTED, across every slice built from one telemetry
	// file. The first call computes it (same math as always); every call after that reuses
	// it instead of recomputing an essentially-constant value up to ~200,000 times over. See
	// the .cpp for why this is safe.
	static constexpr time_t TZ_CORRECTION_UNCOMPUTED = std::numeric_limits<time_t>::min();
	TelemetrySlice(const Json::Value& root, float gps_altitude_offset, time_t& tz_correction);

	int year()         const { return m_timestruct.tm_year + 1900; }
	int month()        const { return m_timestruct.tm_mon + 1; }
	int day()          const { return m_timestruct.tm_mday; }
	int hour()         const { return m_timestruct.tm_hour; }
	int minute()       const { return m_timestruct.tm_min; }
	int second()       const { return m_timestruct.tm_sec; }
	float course_rad() const;  // TODO(P1): precomp and store?
	float speed_mph()  const { return m_speed_kts * 1.15078f; }  // TODO(P1): precomp and store?
	float speed_kts()  const { return m_speed_kts; }
	float speed_kph()  const { return m_speed_kts * 1.852f; }  // TODO(P1): precomp and store?

	std::tm              m_timestruct;
	int                  m_msec;
	float                m_gps_lat;
	float                m_gps_lon;
	float                m_gps_alt;
	float                m_temperature[3]; // Left, Center, Right
	// Altitude shown to the user: m_alt_raw plus the altitude-reference offset (see
	// altitude_reference.h), which TelemetryMgr applies once parsing is done.
	float                m_alt[3];         // Left, Center, Right
	// The barometer exactly as logged. The climb-rate filters and the noise estimate read this,
	// so they never depend on the (non-causal) altitude correction.
	float                m_alt_raw[3];
	float                m_climb_rate[3];  // in ft/min!!! (Yeah, I know, but that's what US varios use.)
	glm::vec3            m_accel;
	glm::vec3            m_gyro;
	bool                 m_pulse;
	float                m_speed_kts;
	float                m_course_deg;
	float                m_total_distance;
	// How many meters wide the map should be at this sample -- see map_zoom.h. Filled in by
	// TelemetryMgr (it depends on earlier samples), not by the constructor.
	float                m_map_width_m;
};

//TODO(P1): TelemetryMgr[x] should return the x-th TelemetrySlice - or a default one.
class TelemetryMgr {
public:
	TelemetryMgr(const std::string& path, std::vector<WidgetBase*>* widgets, float initial_offset, float initial_window_start_elapsed, const ClimbFilterSettings& initial_climb_settings, const MapZoomSettings& initial_map_zoom_settings);
	~TelemetryMgr();

	//Using a * to widgets instead of & since threads don't seem to like refs.
	void parse_telemetry_file(const std::string& path, std::vector<WidgetBase*>* widgets);

	//TODO(P1) get this behind an interface instead of public.
	const TelemetrySlice& operator[](int64_t index) const;
	// Reads the atomic commit count rather than m_telemetry.size() -- see m_committed_size
	// below for why. Safe to call from the render thread while the parse thread is still
	// filling m_telemetry in the background.
	uint32_t size() const { return m_committed_size.load(std::memory_order_acquire); }

	/*
	glm::vec2 get_current_coords();
	std::pair<int32_t, int32_t> get_current_gridref();  // Really, only useful for debugging.
	*/

	// The fixed time offset between the video's clock and the telemetry device's clock (they aren't
	// hardware-synced, so this has to be set by eye -- see TelemetryScrubWidget). Positive means the
	// telemetry device started recording later than the camera did.
	float offset() const                  { return m_telemetry_offset; }
	bool  set_offset(float offset)        { m_telemetry_offset = offset; return true; }

	// Telemetry's own elapsed seconds marking where the usable window of telemetry begins --
	// set once, whenever the launch marker is (re)placed (see EnvConfig::launch_time()), and
	// otherwise held fixed, so it never drifts just because the TSW's sync offset gets
	// tweaked afterward.
	float window_start_elapsed() const           { return m_window_start_elapsed; }
	bool  set_window_start_elapsed(float elapsed) { m_window_start_elapsed = elapsed; return true; }

	// How each slice's m_climb_rate is smoothed -- see climb_filter.h. The filters are strictly
	// causal (a slice's rate depends only on that slice and earlier ones), so what the overlay
	// shows at any moment is something a real instrument could have shown at that moment.
	const ClimbFilterSettings& climb_filter_settings() const { return m_climb_settings; }
	// Re-runs the filters over every slice with new settings. Only legal once the background
	// parse has finished (returns false, changing nothing, otherwise). Bumps climb_version().
	bool  set_climb_filter_settings(const ClimbFilterSettings& settings);
	// How the map zooms in on circling and back out in straight flight -- see map_zoom.h.
	// Same rules as the climb filter: strictly causal, and changing it re-runs the whole
	// recording (returns false, changing nothing, until parsing_done()).
	const MapZoomSettings& map_zoom_settings() const { return m_map_zoom_settings; }
	bool  set_map_zoom_settings(const MapZoomSettings& settings);
	// Incremented every time the stored climb rates are recomputed, so anything that baked them
	// into geometry (the map's course colors) knows to rebuild.
	uint32_t climb_version() const        { return m_climb_version; }
	// Barometer noise (meters rms) measured from this recording's center channel, valid once
	// parsing_done(). Used to tell the user what a given filter setting will cost in noise.
	float alt_noise_sigma_m() const       { return m_alt_noise_sigma_m; }

	// Re-bases m_alt on the user's true-altitude references at two sample indices (normally the
	// video's start and end) -- see altitude_reference.h. Cheap to call every frame: it does
	// nothing unless the references changed or an index moved by more than a few samples. Main
	// thread only; returns false, changing nothing, until parsing_done(). Bumps altitude_version()
	// whenever it actually changes the stored altitudes.
	bool  set_altitude_references(const AltitudeReferences& refs, int32_t start_index, int32_t end_index);
	// Incremented whenever the stored altitudes change, so anything that baked them into geometry
	// (the altitude graph) knows to rebuild.
	uint32_t altitude_version() const     { return m_alt_version; }
	const AltitudeCorrectionInfo& altitude_correction_info() const { return m_alt_info; }

	// Telemetry's own recorded length, in seconds -- independent of the video entirely.
	float duration() const                { return (float)size() / (float)TELEMETRY_FREQUENCY; }

	// Sample index for a plain telemetry-elapsed-seconds value -- no offset involved (contrast
	// with index_at(), which subtracts offset first). Used to turn window_start_elapsed() (and
	// window_start_elapsed() + video duration) into sample indices.
	int32_t index_for_elapsed(float telemetry_elapsed) const { return (int32_t)(telemetry_elapsed * TELEMETRY_FREQUENCY); }

	// Everything below combines telemetry's clock with the caller's notion of "now" on the video's
	// clock (media_elapsed), rather than reaching for a MediaContainerMgr directly, so this class
	// stays decoupled from anything video-related.
	float elapsed_at(float media_elapsed) const;
	// [0..1] across telemetry's own duration, clamped so an offset that pushes "now" outside the
	// telemetry recording still yields a sane, renderable position instead of needing a special case
	// downstream (e.g. in TelemetryScrubWidget's shader).
	float parametric_at(float media_elapsed) const;
	int32_t index_at(float media_elapsed) const;
	const TelemetrySlice& slice_at(float media_elapsed) const { return (*this)[index_at(media_elapsed)]; }

	void tick();
	bool parsing_done() { return m_parse_done && !m_thread_running; }
	static TelemetryMgr* instance;

private:
	std::vector<TelemetrySlice> m_telemetry;
	// How many entries of m_telemetry are actually safe to read right now. The parse thread
	// reserve()s m_telemetry's final size up front (known after its first pass over the
	// file), so push_back() never reallocates and never invalidates anything the render
	// thread might be reading -- but the render thread still has no business reading an
	// index the parse thread hasn't finished constructing yet, and m_telemetry.size() itself
	// isn't safe to read concurrently with push_back(). This is what operator[]/size() read
	// instead: the parse thread bumps it (release) only after a slice is fully written, and
	// readers load it (acquire) first and never index past it. This -- not a mutex -- is what
	// actually fixes the "crashes in the first ~15 seconds" race: that window is exactly how
	// long the background parse used to take, with the render thread reading m_telemetry the
	// whole time with no synchronization at all.
	std::atomic<uint32_t>       m_committed_size;
	std::thread                 m_parse_thread;
	std::atomic<bool>           m_parse_done;
	std::atomic<bool>           m_thread_running;
	TelemetrySlice              m_default_slice;
	float                        m_telemetry_offset;
	float                        m_window_start_elapsed;

	ClimbFilterSettings          m_climb_settings;
	uint32_t                     m_climb_version;
	float                        m_alt_noise_sigma_m;
	MapZoomSettings              m_map_zoom_settings;
	AltitudeReferences           m_alt_refs;            // What the stored m_alt currently reflects.
	int32_t                      m_alt_applied_start_index;
	int32_t                      m_alt_applied_end_index;
	AltitudeOffsetModel          m_alt_model;
	AltitudeCorrectionInfo       m_alt_info;
	uint32_t                     m_alt_version;
	bool                         mean_raw_center_alt(int32_t index, float* mean) const;
	void                         recompute_map_zoom();
	void                         recompute_climb_rates();
	void                         estimate_alt_noise();

	// Per-key auto-repeat state for the held offset-nudge keys -- see ffsw::held_repeat_due().
	int                          m_advance_offset_repeat_bucket;
	int                          m_rewind_offset_repeat_bucket;
};