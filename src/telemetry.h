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
	float                m_alt[3];         // Left, Center, Right
	float                m_climb_rate[3];  // in ft/min!!! (Yeah, I know, but that's what US varios use.)
	glm::vec3            m_accel;
	glm::vec3            m_gyro;
	bool                 m_pulse;
	float                m_speed_kts;
	float                m_course_deg;
	float                m_total_distance;
};

//TODO(P1): TelemetryMgr[x] should return the x-th TelemetrySlice - or a default one.
class TelemetryMgr {
public:
	TelemetryMgr(const std::string& path, std::vector<WidgetBase*>* widgets, float initial_offset, float initial_window_start_elapsed);
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

	// Per-key auto-repeat state for the held offset-nudge keys -- see ffsw::held_repeat_due().
	int                          m_advance_offset_repeat_bucket;
	int                          m_rewind_offset_repeat_bucket;
};