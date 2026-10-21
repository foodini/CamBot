#define _USE_MATH_DEFINES
#include "math.h"
#include "telemetry.h"
#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <json/json.h>
#include <memory>
#include <vector>

#include "env_config.h"
#include "interaction_manager.h"
#include "widget_base.h"

TelemetrySlice::TelemetrySlice() :
	m_timestruct(),
	m_gps_lat(0.0),
	m_gps_lon(0.0),
	m_gps_alt(0.0),
	m_accel(),
	m_gyro(),
	m_pulse(false),
	m_speed_kts(0.0),
	m_course_deg(0.0),
	m_temperature{ 0.0, 0.0, 0.0 },
	m_alt{ 0.0, 0.0, 0.0 },
	m_alt_raw{ 0.0, 0.0, 0.0 },
	m_climb_rate{ 0.0, 0.0, 0.0 },
	m_total_distance(0.0),
	m_map_width_m(2000.0f)
{
}

// A barometer channel with no sensor attached logs 99999.9 for its altitude; anything that
// far out of range is treated as "no data" rather than as an altitude.
static bool alt_sample_valid(float alt_m) {
	return alt_m > -9000.0f && alt_m < 90000.0f;
}

//TODO(P2) init climb rate? (getting a warning that's worth looking at.)
// root is already-parsed JSON (see parse_telemetry_file -- it builds one Json::CharReader
// and reuses it across every line, rather than this constructor building a fresh
// istringstream + locale + reader per call, which used to be most of the per-line cost).
TelemetrySlice::TelemetrySlice(const Json::Value& root, float gps_altitude_offset, time_t& tz_correction) :
	m_pulse(false),
	m_gps_alt(-1000000.0f)
{
	// Filled in later by TelemetryMgr (see recompute_climb_rates()/parse_telemetry_file()).
	m_climb_rate[0] = m_climb_rate[1] = m_climb_rate[2] = 0.0f;
	m_map_width_m = 2000.0f;
	// lat_dir/lon_dir are still needed; decimal_point/unit/open_brace/close_brace/ms/baud used
	// to be parsed here under an older, text-scanned telemetry format and are long gone.
	char lat_dir, lon_dir;

	unsigned int gps_date = root.get("gps_date", "19700101").asUInt();
	double gps_time_d = root.get("gps_time", "0.0").asDouble();
	unsigned int gps_time_i = (unsigned int)gps_time_d;

	m_msec = (int)((gps_time_d - gps_time_i) * 1000);

	m_timestruct.tm_mday = gps_date % 100;
	gps_date /= 100;
	m_timestruct.tm_mon = (gps_date % 100) - 1;
	gps_date /= 100;
	m_timestruct.tm_year = gps_date - 1900;

	m_timestruct.tm_sec = gps_time_i % 100;
	gps_time_i /= 100;
	m_timestruct.tm_min = gps_time_i % 100;
	gps_time_i /= 100;
	m_timestruct.tm_hour = gps_time_i;

	m_gps_lat = root.get("gps_lat", "0.0").asFloat();
	lat_dir = root.get("gps_lat_dir", "N").asString()[0];
	if (lat_dir == 'S')
		m_gps_lat = -m_gps_lat;
	m_gps_lon = root.get("gps_lon", "0.0").asFloat();
	lon_dir = root.get("gps_lon_dir", "E").asString()[0];
	if (lon_dir == 'W')
		m_gps_lon = -m_gps_lon;

	m_gps_alt = root.get("gps_alt", "-1000000.0").asFloat();
	m_speed_kts = root.get("gps_kts", "0.0").asFloat();
	m_course_deg = root.get("gps_dir", "0.0").asFloat();
	m_alt[0] = root.get("ch0_alt_m", "0.0").asFloat();
	m_alt[1] = root.get("ctr_alt_m", "0.0").asFloat();
	m_alt[2] = root.get("ch1_alt_m", "0.0").asFloat();
	m_temperature[0] = root.get("ch0_temp_C", "0.0").asFloat();
	m_temperature[1] = root.get("ctr_temp_C", "0.0").asFloat();
	m_temperature[2] = root.get("ch1_temp_C", "0.0").asFloat();
	m_accel.x = root.get("a_x", "0.0").asFloat();
	m_accel.y = root.get("a_y", "0.0").asFloat();
	m_accel.z = root.get("a_z", "0.0").asFloat();
	m_gyro.x = root.get("r_x", "0.0").asFloat();
	m_gyro.y = root.get("r_y", "0.0").asFloat();
	m_gyro.z = root.get("r_z", "0.0").asFloat();

	//TODO(P0): Eventually, I need to have this adjust over time. The ambient barometric
	//          pressure can change considerably over the course of a flight.
	for (int alt_chan = 0; alt_chan < 3; alt_chan++) {
		m_alt[alt_chan] += gps_altitude_offset;
		m_alt_raw[alt_chan] = m_alt[alt_chan];
	}

	// Work out the difference between local and gmt. Remove twice that difference from the
	// computed time to counter the fact that the conversion is taking us backward. Since
	// the conversoin assumes we're giving it a local time and want a GMT out of it, it's 
	// going the wrong direction. This will have bugs, especially if you're eding video
	// on the other side of a DST change. 
	m_timestruct.tm_isdst = -1; 
	time_t local_epoch = mktime(&m_timestruct); // Assumes that the input is LOCALTIME
	// tz_correction is 2*(mktime(gmtime(local_epoch)) - local_epoch) -- the local/GMT offset,
	// as this (slightly odd) double round-trip computes it. That depends on the system's
	// timezone and whatever DST state is in effect, not on which sample this is, so within one
	// recording it's effectively constant: compute it from the first sample and reuse it for
	// every other one, instead of redoing 2 of these 3 time-API calls up to ~200,000 times for
	// the same answer. (Still has the pre-existing DST-boundary caveat noted above -- a flight
	// that itself spans a DST change was never handled right either way.)
	if (tz_correction == TZ_CORRECTION_UNCOMPUTED) {
		std::tm* new_timestruct = gmtime(&local_epoch); // Output provided assumed input was GMT.
		time_t diff_epoch = mktime(new_timestruct);
		tz_correction = 2 * (diff_epoch - local_epoch);
	}
	local_epoch -= tz_correction;
	std::tm* true_local = gmtime(&local_epoch);

	m_timestruct.tm_year = true_local->tm_year;
	m_timestruct.tm_mon = true_local->tm_mon;
	m_timestruct.tm_mday = true_local->tm_mday;
	m_timestruct.tm_hour = true_local->tm_hour;
}

float TelemetrySlice::course_rad() const {
	return 3.14159265f * (360.0f - m_course_deg) / 180.0f + 3.14159265f/2.0f;
}

TelemetryMgr* TelemetryMgr::instance = nullptr;

TelemetryMgr::TelemetryMgr(const std::string& path, std::vector<WidgetBase*>* widgets, float initial_offset, float initial_window_start_elapsed, const ClimbFilterSettings& initial_climb_settings, const MapZoomSettings& initial_map_zoom_settings) :
	m_committed_size(0),
	m_parse_done(false),
	m_telemetry_offset(initial_offset),
	m_window_start_elapsed(initial_window_start_elapsed),
	m_climb_settings(initial_climb_settings),
	m_climb_version(0),
	m_alt_noise_sigma_m(0.12f),
	m_map_zoom_settings(initial_map_zoom_settings),
	m_alt_applied_start_index(-1),
	m_alt_applied_end_index(-1),
	m_alt_version(0),
	m_advance_offset_repeat_bucket(-1),
	m_rewind_offset_repeat_bucket(-1)
{
	if (TelemetryMgr::instance != nullptr) {
		throw "Cannot create second TelemetryMgr";
	}
	TelemetryMgr::instance = this;

	m_thread_running = true;
	m_parse_thread = std::thread(&TelemetryMgr::parse_telemetry_file, this, path, widgets);
	//parse_telemetry_file(path, widgets);

	// UP/DOWN nudge the video/telemetry sync offset by one telemetry sample at a time -- the
	// finest adjustment that means anything, same idea as the frame-step keys for video. Bound
	// here (rather than in main()) because m_telemetry_offset is this class's own state. UP
	// "advances" telemetry: elapsed_at(media_elapsed) = media_elapsed - offset, so moving
	// telemetry's sample forward relative to the video means decreasing the offset; DOWN
	// "rewinds" it by increasing the offset the same amount. See ffsw::held_repeat_due() for the
	// ~10/s auto-repeat while held.
	// constexpr (not just const) so the lambdas below can use it without capturing it.
	static constexpr float SAMPLE_SECONDS = 1.0f / TELEMETRY_FREQUENCY;
	InteractionMgr::instance()->bind_key(GLFW_KEY_UP,
		[this]() { m_telemetry_offset -= SAMPLE_SECONDS; },
		nullptr,
		[this](float held_seconds) {
			if (ffsw::held_repeat_due(held_seconds, 0.25f, 0.1f, m_advance_offset_repeat_bucket))
				m_telemetry_offset -= SAMPLE_SECONDS;
		});
	InteractionMgr::instance()->bind_key(GLFW_KEY_DOWN,
		[this]() { m_telemetry_offset += SAMPLE_SECONDS; },
		nullptr,
		[this](float held_seconds) {
			if (ffsw::held_repeat_due(held_seconds, 0.25f, 0.1f, m_rewind_offset_repeat_bucket))
				m_telemetry_offset += SAMPLE_SECONDS;
		});
}

TelemetryMgr::~TelemetryMgr() {
	TelemetryMgr::instance = nullptr;
}

void TelemetryMgr::parse_telemetry_file(const std::string& path, std::vector<WidgetBase*>* widgets) {
	// So we can actually measure the effect of parsing changes instead of going by feel.
	auto parse_start_time = std::chrono::steady_clock::now();

	//TODO(P2): User needs to know if there were issues with the file.
	//TODO(P2): Check that the file exists (and feedback to the user.)
	//TODO(P0): This accempts a non-existent file as being empty. Complain if file 1) DNE or 2) empty.
	std::fstream infile(path);
	std::string line;
	std::vector<std::string> lines;

	// I do this as two separate loops because the read is fast, but construction of the slices is slow and,
	// if I want to get polygonalization started early, I need a full count of the number of lines before I
	// start sending slices to Widgets for processing.
	while (std::getline(infile, line)) {
		if (line[0] != '#') {
			lines.push_back(line);
		}
	}

	// m_telemetry.reserve()d to its final size (known now, from the pass above) before any
	// push_back() below -- so push_back() never reallocates, and the render thread reading
	// m_telemetry concurrently (see m_committed_size) never has the rug pulled out from under
	// it. One Json::CharReader built once and reused for every line, instead of rebuilding an
	// istringstream + locale + reader per line -- that reconstruction (the locale in
	// particular) used to dominate the per-line cost far more than the JSON parsing itself.
	m_telemetry.reserve(lines.size());
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	// See TelemetrySlice::TZ_CORRECTION_UNCOMPUTED -- computed once, from the first line, and
	// reused for every line after that.
	time_t tz_correction = TelemetrySlice::TZ_CORRECTION_UNCOMPUTED;

	uint32_t index = 0;
	float gps_altitude_offset = 0.0;
	// One causal filter per barometer channel (left, center, right). m_climb_settings is only
	// changed by set_climb_filter_settings(), which refuses to run until this thread is done.
	ClimbFilter climb_filters[3];
	for (int channel = 0; channel < 3; channel++)
		climb_filters[channel].configure(m_climb_settings, (float)TELEMETRY_FREQUENCY);
	// Likewise m_map_zoom_settings, which set_map_zoom_settings() won't touch until we're done.
	MapZoomTracker map_zoom_tracker(m_map_zoom_settings, (float)TELEMETRY_FREQUENCY);
	for (auto i = lines.begin(); i != lines.end(); index++, i++) {
		Json::Value root;
		std::string errs;
		reader->parse(i->data(), i->data() + i->size(), &root, &errs);
		TelemetrySlice slice = TelemetrySlice(root, gps_altitude_offset, tz_correction);

		if (index == 0) {
			// TODO(P1): work out what to do about correcting for barometric uncertainty using gps.
			// gps_altitude_offset = slice.m_gps_alt - slice.m_alt[1];
			// slice = TelemetrySlice(root, gps_altitude_offset, tz_correction);
			slice.m_total_distance = 0.0;
		} else {
			slice.m_total_distance =
				slice.speed_kph() / 36000.0f +
				(*TelemetryMgr::instance)[index - 1].m_total_distance;
		}

		// The climb rate has to be filled in *before* the slice is copied into m_telemetry (it used to
		// be written afterward, which only reached the local copy that went to the widgets, and left
		// the stored slice -- what the climb indicator and map text read -- holding whatever
		// happened to be in that stack slot from the previous iteration).
		for (int channel = 0; channel < 3; channel++) {
			slice.m_climb_rate[channel] = climb_filters[channel].update(slice.m_alt_raw[channel], alt_sample_valid(slice.m_alt_raw[channel])) *
				METERS_PER_SECOND_TO_FEET_PER_MINUTE;
		}
		slice.m_map_width_m = map_zoom_tracker.update(slice.m_gps_lat, slice.m_gps_lon, slice.m_speed_kts, slice.m_course_deg);

		m_telemetry.push_back(slice);
		// Only safe to publish *after* the slice above is fully written into m_telemetry --
		// see m_committed_size's comment in telemetry.h.
		m_committed_size.store(index + 1, std::memory_order_release);
		for (auto widget = widgets->begin(); widget != widgets->end(); widget++) {
			(*widget)->polygonalize(slice, index, (uint32_t)lines.size());
		}
	}
	m_default_slice = m_telemetry[0];
	estimate_alt_noise();

	double parse_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - parse_start_time).count();
	printf("Telemetry parse: %u lines in %.1f ms\n", (uint32_t)lines.size(), parse_ms);

	//TODO(P2): Reap the thread when it's done. Better to do this when TelemetryMgr is receiving regular
	//          calls and can periodically look at the bool & attempt to join().
	m_parse_done = true;
}

// Barometer noise, from the center channel's second differences: for independent noise of
// sigma per sample, a[i+1] - 2a[i] + a[i-1] has standard deviation sigma*sqrt(6), and real
// altitude changes barely register in a second difference at 10 Hz. The median of the absolute
// values (median|x| = 0.6745 sigma for a Gaussian) keeps the occasional outlier from skewing it.
void TelemetryMgr::estimate_alt_noise() {
	std::vector<float> second_diffs;
	second_diffs.reserve(m_telemetry.size());
	for (size_t i = 1; i + 1 < m_telemetry.size(); i++) {
		float before = m_telemetry[i - 1].m_alt_raw[1];
		float here = m_telemetry[i].m_alt_raw[1];
		float after = m_telemetry[i + 1].m_alt_raw[1];
		if (alt_sample_valid(before) && alt_sample_valid(here) && alt_sample_valid(after))
			second_diffs.push_back(fabsf(after - 2.0f * here + before));
	}
	if (second_diffs.size() < 100)
		return;  // Too little to go on; keep the default.
	std::nth_element(second_diffs.begin(), second_diffs.begin() + second_diffs.size() / 2, second_diffs.end());
	float median_abs = second_diffs[second_diffs.size() / 2];
	float sigma = median_abs / 0.6745f / sqrtf(6.0f);
	if (sigma > 0.001f)
		m_alt_noise_sigma_m = sigma;
}

// How much barometer to average around a reference point: +/- this many samples (2.5 s). Wide
// enough to take the noise down to centimeters, narrow enough that the glider hasn't gone
// anywhere in the meantime if the reference is taken on the ground.
#define ALTITUDE_REF_HALF_WINDOW_SAMPLES 25

// Mean of the valid center-channel barometer readings around index (clamped to the recording).
// False if there are none.
bool TelemetryMgr::mean_raw_center_alt(int32_t index, float* mean) const {
	const int32_t count = (int32_t)size();
	if (count == 0)
		return false;
	int32_t first = (std::max)(0, index - ALTITUDE_REF_HALF_WINDOW_SAMPLES);
	int32_t last = (std::min)(count - 1, index + ALTITUDE_REF_HALF_WINDOW_SAMPLES);
	if (first > last) {
		// The reference is far outside the recording; use the nearest edge.
		first = last = (index < 0) ? 0 : (count - 1);
	}
	double sum = 0.0;
	int32_t n = 0;
	for (int32_t i = first; i <= last; i++) {
		float alt = m_telemetry[i].m_alt_raw[1];
		if (alt_sample_valid(alt)) {
			sum += alt;
			n++;
		}
	}
	if (n == 0)
		return false;
	*mean = (float)(sum / n);
	return true;
}

bool TelemetryMgr::set_altitude_references(const AltitudeReferences& refs, int32_t start_index, int32_t end_index) {
	if (!parsing_done())
		return false;

	const bool wants_none = !refs.has_start() && !refs.has_end();
	if (wants_none && !m_alt_model.active()) {
		m_alt_refs = refs;
		return true;
	}
	// Nudging the video/telemetry sync offset moves the video's ends a sample at a time; that
	// doesn't change the answer enough to be worth redoing the whole file for.
	static const int32_t INDEX_SLACK = 20;
	if (refs == m_alt_refs &&
		abs(start_index - m_alt_applied_start_index) <= INDEX_SLACK &&
		abs(end_index - m_alt_applied_end_index) <= INDEX_SLACK)
		return true;

	auto start_time = std::chrono::steady_clock::now();
	const uint32_t count = size();
	if (count == 0)
		return true;

	m_alt_model.clear();
	m_alt_info = AltitudeCorrectionInfo();
	float mean = 0.0f;
	if (refs.has_start() && mean_raw_center_alt(start_index, &mean)) {
		m_alt_info.has_start = true;
		m_alt_info.start_baro_m = mean;
		m_alt_info.start_offset_m = refs.start_m() - mean;
		m_alt_model.set_start(start_index, m_alt_info.start_offset_m);
	}
	if (refs.has_end() && mean_raw_center_alt(end_index, &mean)) {
		m_alt_info.has_end = true;
		m_alt_info.end_baro_m = mean;
		m_alt_info.end_offset_m = refs.end_m() - mean;
		m_alt_model.set_end(end_index, m_alt_info.end_offset_m);
	}

	for (uint32_t i = 0; i < count; i++) {
		TelemetrySlice& slice = m_telemetry[i];
		const float offset = m_alt_model.offset_at((int32_t)i);
		for (int channel = 0; channel < 3; channel++) {
			// A channel with no sensor attached keeps its "no data" value untouched.
			slice.m_alt[channel] = alt_sample_valid(slice.m_alt_raw[channel]) ? slice.m_alt_raw[channel] + offset : slice.m_alt_raw[channel];
		}
	}
	m_default_slice = m_telemetry[0];

	m_alt_refs = refs;
	m_alt_applied_start_index = start_index;
	m_alt_applied_end_index = end_index;
	m_alt_version++;
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
	printf("Altitude correction: %u samples in %.1f ms\n", count, ms);
	return true;
}

bool TelemetryMgr::set_map_zoom_settings(const MapZoomSettings& settings) {
	if (!parsing_done())
		return false;
	m_map_zoom_settings = settings;
	recompute_map_zoom();
	return true;
}

// Re-runs the map zoom from the first slice, in order, exactly as the parse did. Main thread
// only, after parsing_done().
void TelemetryMgr::recompute_map_zoom() {
	auto start_time = std::chrono::steady_clock::now();
	MapZoomTracker tracker(m_map_zoom_settings, (float)TELEMETRY_FREQUENCY);
	const uint32_t count = size();
	for (uint32_t i = 0; i < count; i++) {
		TelemetrySlice& slice = m_telemetry[i];
		slice.m_map_width_m = tracker.update(slice.m_gps_lat, slice.m_gps_lon, slice.m_speed_kts, slice.m_course_deg);
	}
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
	printf("Map zoom recompute: %u samples in %.1f ms\n", count, ms);
}

bool TelemetryMgr::set_climb_filter_settings(const ClimbFilterSettings& settings) {
	if (!parsing_done())
		return false;
	m_climb_settings = settings;
	recompute_climb_rates();
	return true;
}

// Re-runs the climb filters from the first slice, in order, exactly as the parse did. Main
// thread only, after parsing_done(), so nothing else is touching m_telemetry.
void TelemetryMgr::recompute_climb_rates() {
	auto start_time = std::chrono::steady_clock::now();
	ClimbFilter climb_filters[3];
	for (int channel = 0; channel < 3; channel++)
		climb_filters[channel].configure(m_climb_settings, (float)TELEMETRY_FREQUENCY);
	const uint32_t count = size();
	for (uint32_t i = 0; i < count; i++) {
		TelemetrySlice& slice = m_telemetry[i];
		for (int channel = 0; channel < 3; channel++) {
			slice.m_climb_rate[channel] = climb_filters[channel].update(slice.m_alt_raw[channel], alt_sample_valid(slice.m_alt_raw[channel])) *
				METERS_PER_SECOND_TO_FEET_PER_MINUTE;
		}
	}
	m_climb_version++;
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
	printf("Climb rate recompute: %u samples in %.1f ms\n", count, ms);
}

const TelemetrySlice& TelemetryMgr::operator[](int64_t index) const {
	// if telemetry is empty, or we're indexing outside its bounds, return a thing that indicates no data available.
	// size() reads m_committed_size, not m_telemetry.size() -- see that member's comment.
	if ((uint64_t)index >= size() || index < 0)
		return m_default_slice;
	return m_telemetry[index];
}

float TelemetryMgr::elapsed_at(float media_elapsed) const {
	return media_elapsed - m_telemetry_offset;
}

float TelemetryMgr::parametric_at(float media_elapsed) const {
	float total_duration = duration();
	if (total_duration <= 0.0f) {
		return 0.0f;
	}
	float parametric = elapsed_at(media_elapsed) / total_duration;
	if (parametric < 0.0f) {
		parametric = 0.0f;
	} else if (parametric > 1.0f) {
		parametric = 1.0f;
	}
	return parametric;
}

int32_t TelemetryMgr::index_at(float media_elapsed) const {
	return (int32_t)(elapsed_at(media_elapsed) * TELEMETRY_FREQUENCY);
}

void TelemetryMgr::tick() {
	if (m_thread_running and m_parse_done) {
		m_parse_thread.join();
		m_thread_running = false;
	}
}