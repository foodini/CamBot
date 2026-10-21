#pragma once

// The end-of-video flight summary: a block of text (label on the left, value on the right) drawn
// over the last few seconds of the video, fading in over the first second. The numbers come from
// flight_stats.h, over the same telemetry window as the altitude graph, and are recomputed only
// when something they depend on changes (the window, the launch marker, the climb filter, or the
// altitude references). Everything is a function of the video position, so scrubbing and export
// give the same picture. Drawn through the FontManager, like the rest of the overlay text.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "env_config.h"
#include "flight_stats.h"
#include "font_manager.h"
#include "telemetry.h"
#include "util.h"

class FlightSummaryOverlay {
public:
	// bottom_ndc: screen-space y (-1..1) of the last line's baseline -- the altitude graph's
	// bottom edge, so the two sit the same distance off the bottom of the video.
	explicit FlightSummaryOverlay(float bottom_ndc) :
		m_bottom_ndc(bottom_ndc), m_have_summary(false),
		m_start(-1), m_end(-1), m_launch(-1), m_climb_version(0), m_alt_version(0)
	{}

	void render() {
		EnvConfig* env_config = EnvConfig::instance;
		TelemetryMgr* telemetry_mgr = TelemetryMgr::instance;
		if (env_config == nullptr || telemetry_mgr == nullptr || !telemetry_mgr->parsing_done())
			return;

		const SummarySettings& settings = env_config->summary_settings();
		const float duration = env_config->media_in_duration();
		const float elapsed = env_config->media_in_elapsed();
		const float shown_from = duration - settings.seconds;
		if (duration <= 0.0f || elapsed < shown_from)
			return;
		const float alpha = (std::max)(0.0f, (std::min)(1.0f, (elapsed - shown_from) / FADE_IN_SECONDS));
		if (alpha <= 0.0f)
			return;

		refresh();
		if (!m_summary.valid)
			return;

		// Flight time as the top-right readout would show it on the last frame.
		const float flight_seconds = (std::max)(0.0f, env_config->flight_time() + (duration - elapsed));
		const std::vector<std::pair<std::string, std::string>> rows = build_rows(m_summary, flight_seconds, settings.show_xc);
		draw(rows, alpha);
	}

private:
	static constexpr float FADE_IN_SECONDS = 1.0f;
	static constexpr float BASE_SCALE = 0.75f;      // Text scale (the other overlays use 0.5).
	static constexpr float TITLE_SCALE_FACTOR = 1.2f;
	static constexpr float LINE_SPACING = 1.25f;    // Line height, as a multiple of the font size.
	static constexpr float MAX_HEIGHT_FRACTION = 0.7f;  // The block never takes more of the video than this.
	static constexpr float DROP_SHADOW_PIXELS = 2.0f;
	static constexpr int   GAP_CHARACTERS = 3;      // Minimum space between the longest label and value.

	static std::string clock(float seconds) {
		char buf[32];
		ffsw::make_time(buf, seconds, false);
		return std::string(buf);
	}

	static std::vector<std::pair<std::string, std::string>> build_rows(const FlightSummary& s, float flight_seconds, bool show_xc) {
		const float feet_per_meter = 3.28084f;
		std::vector<std::pair<std::string, std::string>> rows;
		rows.push_back(std::make_pair("Flight time", clock(flight_seconds)));
		rows.push_back(std::make_pair("Distance flown (mi)", ffsw::format("%.1f", s.distance_flown_mi)));
		rows.push_back(std::make_pair("Min altitude (ft)", ffsw::format("%.0f", s.min_alt_m * feet_per_meter)));
		rows.push_back(std::make_pair("Max altitude (ft)", ffsw::format("%.0f", s.max_alt_m * feet_per_meter)));
		rows.push_back(std::make_pair("Max height above launch (ft)", ffsw::format("%.0f", s.max_height_above_launch_m * feet_per_meter)));
		rows.push_back(std::make_pair("Max climb (ft/min)", ffsw::format("%+.0f", s.max_climb_fpm)));
		rows.push_back(std::make_pair("Max sink (ft/min)", ffsw::format("%+.0f", s.max_sink_fpm)));
		rows.push_back(std::make_pair("Max ground speed (mph)", ffsw::format("%.1f", s.max_speed_mph)));
		if (show_xc) {
			rows.push_back(std::make_pair("Distance covered (mi)", ffsw::format("%.1f", s.distance_covered_mi)));
			rows.push_back(std::make_pair("Best ground glide", s.has_glide ? ffsw::format("%.1f : 1", s.best_glide_ratio) : std::string("--")));
			rows.push_back(std::make_pair("Circles", ffsw::format("%d", s.circle_count)));
			const int circling_total = (int)(s.circling_seconds + 0.5f);
			const float percent = flight_seconds > 0.0f ? 100.0f * s.circling_seconds / flight_seconds : 0.0f;
			rows.push_back(std::make_pair("Time circling",
				ffsw::format("%d:%02d (%.0f%%)", circling_total / 60, circling_total % 60, percent)));
		}
		return rows;
	}

	// Labels are left-aligned and values right-aligned, as a block centered on the screen, with
	// the title centered above it and the last row's baseline at m_bottom_ndc.
	void draw(const std::vector<std::pair<std::string, std::string>>& rows, float alpha) {
		EnvConfig* env_config = EnvConfig::instance;
		FontManager* font_mgr = env_config->font_mgr;
		const float screen_w = env_config->screen_width();
		const float screen_h = env_config->screen_height();
		const float video_h = screen_h - env_config->ui_height();
		const float font_px = (float)font_mgr->font_size();

		// Lines: the title, a blank line, then the rows.
		const int line_count = (int)rows.size() + 2;
		float scale = BASE_SCALE;
		const float max_height_px = MAX_HEIGHT_FRACTION * video_h;
		if (line_count * font_px * scale * LINE_SPACING > max_height_px)
			scale = max_height_px / (line_count * font_px * LINE_SPACING);
		const float line_px = font_px * scale * LINE_SPACING;

		float label_w = 0.0f, value_w = 0.0f;
		for (const std::pair<std::string, std::string>& row : rows) {
			label_w = (std::max)(label_w, font_mgr->string_width_pixels(row.first, scale));
			value_w = (std::max)(value_w, font_mgr->string_width_pixels(row.second, scale));
		}
		const float gap_w = font_mgr->string_width_pixels(std::string(GAP_CHARACTERS, ' '), scale);
		const float block_w = label_w + gap_w + value_w;
		const float left_ndc = -block_w / screen_w;    // (-block_w / 2) pixels, in NDC (2 / screen_w per pixel)
		const float right_ndc = block_w / screen_w;
		const float line_ndc = line_px * 2.0f / screen_h;

		const glm::vec3 white(1.0f, 1.0f, 1.0f);
		for (int i = 0; i < (int)rows.size(); i++) {
			const float y = m_bottom_ndc + (float)((int)rows.size() - 1 - i) * line_ndc;
			StringAndProperties label(rows[i].first, 0, glm::vec2(left_ndc, y), white, scale, DROP_SHADOW_PIXELS,
				StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT);
			label.alpha = alpha;
			font_mgr->add_string(label);
			StringAndProperties value(rows[i].second, 0, glm::vec2(right_ndc, y), white, scale, DROP_SHADOW_PIXELS,
				StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_RIGHT);
			value.alpha = alpha;
			font_mgr->add_string(value);
		}
		const float title_y = m_bottom_ndc + (float)(rows.size() + 1) * line_ndc;
		StringAndProperties title("Flight Summary", 0, glm::vec2(0.0f, title_y), white, scale * TITLE_SCALE_FACTOR, DROP_SHADOW_PIXELS,
			StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_CENTER);
		title.alpha = alpha;
		font_mgr->add_string(title);
	}

	// Recomputes the statistics if anything they depend on has changed since last time.
	void refresh() {
		const EnvConfig* env_config = EnvConfig::instance;
		TelemetryMgr* telemetry_mgr = TelemetryMgr::instance;
		const int32_t start = env_config->telemetry_window_start_index();
		const int32_t end = env_config->telemetry_window_end_index();
		const int32_t launch = env_config->telemetry_launch_index();
		const uint32_t climb_version = telemetry_mgr->climb_version();
		const uint32_t alt_version = telemetry_mgr->altitude_version();
		if (m_have_summary && start == m_start && end == m_end && launch == m_launch &&
			climb_version == m_climb_version && alt_version == m_alt_version)
			return;

		std::vector<FlightSample> samples;
		if (end >= start)
			samples.reserve((size_t)(end - start + 1));
		for (int32_t i = start; i <= end; i++) {
			const TelemetrySlice& slice = (*telemetry_mgr)[i];
			FlightSample sample;
			sample.lat_deg = slice.m_gps_lat;
			sample.lon_deg = slice.m_gps_lon;
			sample.alt_m = slice.m_alt[1];
			sample.climb_fpm = slice.m_climb_rate[1];
			sample.speed_kts = slice.m_speed_kts;
			sample.course_deg = slice.m_course_deg;
			sample.total_distance_km = slice.m_total_distance;
			samples.push_back(sample);
		}
		// Always computed with the cross-country stats; whether they're shown is a display choice.
		m_summary = flight_stats::compute(samples, launch - start, (float)TELEMETRY_FREQUENCY, true);
		m_have_summary = true;
		m_start = start;
		m_end = end;
		m_launch = launch;
		m_climb_version = climb_version;
		m_alt_version = alt_version;
	}

	float         m_bottom_ndc;
	FlightSummary m_summary;
	bool          m_have_summary;
	int32_t       m_start;
	int32_t       m_end;
	int32_t       m_launch;
	uint32_t      m_climb_version;
	uint32_t      m_alt_version;
};
