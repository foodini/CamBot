#pragma once

// Settings that belong to the person running CamBot rather than to a particular video, so a
// look they've dialed in carries over from project to project: the climb-rate filter, the
// climb color range, the map trace widths, and the map zoom limits. Stored as simple key:value
// lines in %APPDATA%\CamBot\user_settings.txt (or next to the working directory if APPDATA
// isn't set). Missing or out-of-range values fall back to their defaults.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "climb_filter.h"
#include "map_zoom.h"

// How the map's course trace is drawn (see map_widget_1_course.vert): the most recent
// recent_seconds of trace are drawn recent_width_px wide, everything older old_width_px wide,
// and the part the glider hasn't flown yet is muted.
struct MapTraceSettings {
	MapTraceSettings() : recent_width_px(8.0f), old_width_px(3.0f), recent_seconds(120.0f) {}

	float recent_width_px;
	float old_width_px;
	float recent_seconds;
};

// The flight summary shown over the end of the video (see flight_summary_overlay.h): how many
// seconds of the end of the video it covers, and whether it includes the cross-country statistics
// (distance covered, best glide, circles and time circling).
struct SummarySettings {
	SummarySettings() : seconds(10.0f), show_xc(false) {}

	float seconds;
	bool  show_xc;
};

class UserSettings {
public:
	UserSettings() : climb_filter(), climb_color_range(800.0f), map_trace(), map_zoom(), summary() {
		load();
	}

	ClimbFilterSettings climb_filter;
	float               climb_color_range;   // +/- ft/min spanned by the map course colors and the climb indicator
	MapTraceSettings    map_trace;
	MapZoomSettings     map_zoom;
	SummarySettings     summary;

	// Where the settings file lives. create_directory makes the CamBot folder if it's missing.
	static std::string file_path(bool create_directory) {
		const char* appdata = getenv("APPDATA");
		if (appdata == nullptr || appdata[0] == '\0')
			return "cambot_user_settings.txt";
#ifdef _WIN32
		const std::string dir = std::string(appdata) + "\\CamBot";
		if (create_directory)
			_mkdir(dir.c_str());
		return dir + "\\user_settings.txt";
#else
		const std::string dir = std::string(appdata) + "/CamBot";
		if (create_directory)
			mkdir(dir.c_str(), 0755);
		return dir + "/user_settings.txt";
#endif
	}

	void load() {
		FILE* fd = fopen(file_path(false).c_str(), "r");
		if (fd == nullptr)
			return;  // First run: keep the defaults.
		char line[256];
		while (fgets(line, sizeof(line), fd) != nullptr) {
			char* colon = strchr(line, ':');
			if (colon == nullptr)
				continue;
			*colon = '\0';
			const char* key = line;
			const float value = (float)atof(colon + 1);

			if (strcmp(key, "climb_filter") == 0) {
				const int type = (int)value;
				if (type >= 0 && type < CLIMB_FILTER_TYPE_COUNT)
					climb_filter.type = type;
			}
			else if (strcmp(key, "climb_window_s") == 0)     { if (value >= 0.2f && value <= 20.0f)    climb_filter.window_seconds = value; }
			else if (strcmp(key, "climb_tau_s") == 0)        { if (value >= 0.1f && value <= 10.0f)    climb_filter.time_constant_seconds = value; }
			else if (strcmp(key, "climb_color_range") == 0)  { if (value >= 50.0f && value <= 5000.0f) climb_color_range = value; }
			else if (strcmp(key, "trace_recent_px") == 0)    { if (value >= 1.0f && value <= 32.0f)    map_trace.recent_width_px = value; }
			else if (strcmp(key, "trace_old_px") == 0)       { if (value >= 1.0f && value <= 32.0f)    map_trace.old_width_px = value; }
			else if (strcmp(key, "trace_recent_s") == 0)     { if (value >= 5.0f && value <= 3600.0f)  map_trace.recent_seconds = value; }
			else if (strcmp(key, "zoom_min_km") == 0)        { if (value >= 0.2f && value <= 20.0f)    map_zoom.min_width_km = value; }
			else if (strcmp(key, "zoom_max_km") == 0)        { if (value >= 0.2f && value <= 20.0f)    map_zoom.max_width_km = value; }
			else if (strcmp(key, "zoom_transition_s") == 0)  { if (value >= 1.0f && value <= 120.0f)  map_zoom.transition_seconds = value; }
			else if (strcmp(key, "summary_s") == 0)          { if (value >= 1.0f && value <= 120.0f)  summary.seconds = value; }
			else if (strcmp(key, "summary_xc") == 0)         { summary.show_xc = (value != 0.0f); }
		}
		fclose(fd);
		if (map_zoom.max_width_km < map_zoom.min_width_km)
			map_zoom.max_width_km = map_zoom.min_width_km;
	}

	bool save() const {
		FILE* fd = fopen(file_path(true).c_str(), "w");
		if (fd == nullptr)
			return false;
		fprintf(fd, "climb_filter:%d\n", climb_filter.type);
		fprintf(fd, "climb_window_s:%f\n", climb_filter.window_seconds);
		fprintf(fd, "climb_tau_s:%f\n", climb_filter.time_constant_seconds);
		fprintf(fd, "climb_color_range:%f\n", climb_color_range);
		fprintf(fd, "trace_recent_px:%f\n", map_trace.recent_width_px);
		fprintf(fd, "trace_old_px:%f\n", map_trace.old_width_px);
		fprintf(fd, "trace_recent_s:%f\n", map_trace.recent_seconds);
		fprintf(fd, "zoom_min_km:%f\n", map_zoom.min_width_km);
		fprintf(fd, "zoom_max_km:%f\n", map_zoom.max_width_km);
		fprintf(fd, "zoom_transition_s:%f\n", map_zoom.transition_seconds);
		fprintf(fd, "summary_s:%f\n", summary.seconds);
		fprintf(fd, "summary_xc:%d\n", summary.show_xc ? 1 : 0);
		fclose(fd);
		return true;
	}
};
