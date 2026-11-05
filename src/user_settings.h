#pragma once

// Settings that belong to the person running CamBot rather than to a particular video, so a
// look they've dialed in carries over from project to project: the climb-rate filter, the
// climb color range, the map trace widths, the map zoom limits, and the size of the window, and
// which projects were open lately. Stored as simple key:value
// lines in %APPDATA%\CamBot\user_settings.txt (or next to the working directory if APPDATA
// isn't set). Missing or out-of-range values fall back to their defaults.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "climb_filter.h"
#include "recent_projects.h"
#include "map_zoom.h"
#include "text_box.h"
#include "text_box_geometry.h"

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

// How loud the preview plays, 0 to 1, and whether it is muted (the sound of an export is not affected by either).
struct SoundSettings {
	SoundSettings() : volume(1.0f), muted(false) {}

	float volume;
	bool  muted;
};

// The size the main window had when CamBot was last closed, so it opens the same way next time.
// `width` and `height` are the window's normal (not maximized) size; `panel_width` and
// `timeline_height` are how wide the user has dragged the left panel and how tall the timeline
// editor along the bottom.
struct WindowSettings {
	WindowSettings() : width(1200), height(900), maximized(false), panel_width(280), timeline_height(180) {}

	int  width;
	int  height;
	bool maximized;
	int  panel_width;
	int  timeline_height;
};

// What a new text box (see text_box.h) starts out as: how the last one the user worked on looked, so a look they settle on
// carries over from box to box and project to project. It is the box's place and size in the frame, its type, its colours,
// its shadow, and how long it fades in, holds and fades out (in seconds, as the lengths in ticks belong to a project). Also
// the colours lately used for text and shadows, most recent first, as 0xRRGGBB.
struct TextSettings {
	static const int MAX_RECENT_COLORS = 10;

	TextSettings() : x(0.1f), y(0.75f), w(0.8f), h(0.15f), style(), fade_in_seconds(0.5f), dwell_seconds(4.0f), fade_out_seconds(0.5f) {}

	float                x;
	float                y;
	float                w;
	float                h;
	TextStyle            style;
	float                fade_in_seconds;
	float                dwell_seconds;
	float                fade_out_seconds;
	std::vector<int>     recent_colors;

	// Brings a colour to the front of the recent colours (and drops the oldest if there are too many).
	void note_color_used(int rgb) {
		for (size_t i = 0; i < recent_colors.size(); i++) {
			if (recent_colors[i] == rgb) {
				recent_colors.erase(recent_colors.begin() + i);
				break;
			}
		}
		recent_colors.insert(recent_colors.begin(), rgb);
		if ((int)recent_colors.size() > MAX_RECENT_COLORS)
			recent_colors.resize(MAX_RECENT_COLORS);
	}
	// Red, green and blue (0 to 1) as 0xRRGGBB, and back.
	static int pack_color(const float color[3]) {
		int packed = 0;
		for (int i = 0; i < 3; i++) {
			float channel = color[i] < 0.0f ? 0.0f : (color[i] > 1.0f ? 1.0f : color[i]);
			packed = (packed << 8) | (int)(channel * 255.0f + 0.5f);
		}
		return packed;
	}
	static void unpack_color(int rgb, float color[3]) {
		color[0] = (float)((rgb >> 16) & 0xFF) / 255.0f;
		color[1] = (float)((rgb >> 8) & 0xFF) / 255.0f;
		color[2] = (float)(rgb & 0xFF) / 255.0f;
	}
	// The box held inside the frame.
	void clamp_box() {
		const text_box_geometry::Rect box = text_box_geometry::clamp_to_frame(text_box_geometry::Rect(x, y, w, h));
		x = box.x;
		y = box.y;
		w = box.w;
		h = box.h;
	}
};

class UserSettings {
public:
	UserSettings() : climb_filter(), climb_color_range(800.0f), map_dim(0.25f), map_trace(), map_zoom(), summary(), window(), sound(), text_defaults() {
		load();
	}

	ClimbFilterSettings climb_filter;
	float               climb_color_range;   // +/- ft/min spanned by the map course colors and the climb indicator
	float               map_dim;             // How much the sync view darkens the map's picture, 0 (not at all) to 0.9, so the path shows on it
	MapTraceSettings    map_trace;
	MapZoomSettings     map_zoom;
	SummarySettings     summary;
	WindowSettings      window;
	SoundSettings       sound;
	TextSettings        text_defaults;
	// The project to open when CamBot starts (empty after the user closes it deliberately), and the
	// ones opened lately, most recent first. Paths are UTF-8.
	std::string         last_project;
	RecentProjects      recent_projects;

	// A project was opened: it is the one to open next time, and goes to the top of the recent list.
	void note_project_opened(const std::string& path) {
		last_project = path;
		recent_projects.note_opened(path);
	}
	// The user closed the project on purpose: don't open it again at the next start (it stays in the recent list).
	void forget_last_project() { last_project.clear(); }

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

	// The rest of a line after its colon, without the line end.
	static std::string text_string(const char* after_colon) {
		std::string text = after_colon;
		while (!text.empty() && (text[text.size() - 1] == '\n' || text[text.size() - 1] == '\r'))
			text.erase(text.size() - 1);
		return text;
	}

	void load() {
		FILE* fd = fopen(file_path(false).c_str(), "r");
		if (fd == nullptr)
			return;  // First run: keep the defaults.
		char line[2048];   // Long enough for a path.
		while (fgets(line, sizeof(line), fd) != nullptr) {
			char* colon = strchr(line, ':');
			if (colon == nullptr)
				continue;
			*colon = '\0';
			const char* key = line;
			const float value = (float)atof(colon + 1);
			// Some values are text (a path), up to the end of the line.
			std::string text = colon + 1;
			while (!text.empty() && (text[text.size() - 1] == '\n' || text[text.size() - 1] == '\r'))
				text.erase(text.size() - 1);

			if (strcmp(key, "climb_filter") == 0) {
				const int type = (int)value;
				if (type >= 0 && type < CLIMB_FILTER_TYPE_COUNT)
					climb_filter.type = type;
			}
			else if (strcmp(key, "climb_window_s") == 0)     { if (value >= 0.2f && value <= 20.0f)    climb_filter.window_seconds = value; }
			else if (strcmp(key, "climb_tau_s") == 0)        { if (value >= 0.1f && value <= 10.0f)    climb_filter.time_constant_seconds = value; }
			else if (strcmp(key, "climb_color_range") == 0)  { if (value >= 50.0f && value <= 5000.0f) climb_color_range = value; }
			else if (strcmp(key, "map_dim") == 0)            { if (value >= 0.0f && value <= 0.9f)     map_dim = value; }
			else if (strcmp(key, "trace_recent_px") == 0)    { if (value >= 1.0f && value <= 32.0f)    map_trace.recent_width_px = value; }
			else if (strcmp(key, "trace_old_px") == 0)       { if (value >= 1.0f && value <= 32.0f)    map_trace.old_width_px = value; }
			else if (strcmp(key, "trace_recent_s") == 0)     { if (value >= 5.0f && value <= 3600.0f)  map_trace.recent_seconds = value; }
			else if (strcmp(key, "zoom_min_km") == 0)        { if (value >= 0.2f && value <= 20.0f)    map_zoom.min_width_km = value; }
			else if (strcmp(key, "zoom_max_km") == 0)        { if (value >= 0.2f && value <= 20.0f)    map_zoom.max_width_km = value; }
			else if (strcmp(key, "zoom_transition_s") == 0)  { if (value >= 1.0f && value <= 120.0f)  map_zoom.transition_seconds = value; }
			else if (strcmp(key, "summary_s") == 0)          { if (value >= 1.0f && value <= 120.0f)  summary.seconds = value; }
			else if (strcmp(key, "summary_xc") == 0)         { summary.show_xc = (value != 0.0f); }
			else if (strcmp(key, "window_w") == 0)           { if (value >= 400.0f && value <= 16384.0f) window.width = (int)value; }
			else if (strcmp(key, "window_h") == 0)           { if (value >= 300.0f && value <= 16384.0f) window.height = (int)value; }
			else if (strcmp(key, "window_maximized") == 0)   { window.maximized = (value != 0.0f); }
			else if (strcmp(key, "window_panel_w") == 0)     { if (value >= 60.0f && value <= 8000.0f)   window.panel_width = (int)value; }
			else if (strcmp(key, "sound_volume") == 0)       { if (value >= 0.0f && value <= 1.0f)     sound.volume = value; }
			else if (strcmp(key, "sound_muted") == 0)        { sound.muted = (value != 0.0f); }
			else if (strcmp(key, "last_project") == 0)       { last_project = text; }
			else if (strcmp(key, "recent_project") == 0)     { recent_projects.append(text); }
			else if (strcmp(key, "window_timeline_h") == 0)  { if (value >= 60.0f && value <= 8000.0f)   window.timeline_height = (int)value; }
			else if (strcmp(key, "text_x") == 0)             { text_defaults.x = value; }
			else if (strcmp(key, "text_y") == 0)             { text_defaults.y = value; }
			else if (strcmp(key, "text_w") == 0)             { text_defaults.w = value; }
			else if (strcmp(key, "text_h") == 0)             { text_defaults.h = value; }
			else if (strcmp(key, "text_font") == 0)          { if (value >= 0.0f && value < 1000.0f)   text_defaults.style.font = (int)value; }
			else if (strcmp(key, "text_size") == 0)          { if (value >= 0.005f && value <= 0.5f)   text_defaults.style.size = value; }
			else if (strcmp(key, "text_bold") == 0)          { text_defaults.style.bold = (value != 0.0f); }
			else if (strcmp(key, "text_italic") == 0)        { text_defaults.style.italic = (value != 0.0f); }
			else if (strcmp(key, "text_strike") == 0)        { text_defaults.style.strike = (value != 0.0f); }
			else if (strcmp(key, "text_align") == 0)         { if (value >= 0.0f && value <= 2.0f)     text_defaults.style.align = (int)value; }
			else if (strcmp(key, "text_color") == 0)         { TextSettings::unpack_color((int)strtol(text_string(colon + 1).c_str(), nullptr, 16), text_defaults.style.color); }
			else if (strcmp(key, "text_shadow_color") == 0)  { TextSettings::unpack_color((int)strtol(text_string(colon + 1).c_str(), nullptr, 16), text_defaults.style.shadow_color); }
			else if (strcmp(key, "text_shadow_opacity") == 0) { if (value >= 0.0f && value <= 1.0f)    text_defaults.style.shadow_opacity = value; }
			else if (strcmp(key, "text_shadow_dx") == 0)     { if (value >= -0.2f && value <= 0.2f)    text_defaults.style.shadow_dx = value; }
			else if (strcmp(key, "text_shadow_dy") == 0)     { if (value >= -0.2f && value <= 0.2f)    text_defaults.style.shadow_dy = value; }
			else if (strcmp(key, "text_fade_in_s") == 0)     { if (value >= 0.0f && value <= 600.0f)   text_defaults.fade_in_seconds = value; }
			else if (strcmp(key, "text_dwell_s") == 0)       { if (value >= 0.0f && value <= 3600.0f)  text_defaults.dwell_seconds = value; }
			else if (strcmp(key, "text_fade_out_s") == 0)    { if (value >= 0.0f && value <= 600.0f)   text_defaults.fade_out_seconds = value; }
			else if (strcmp(key, "text_recent_color") == 0)  { if ((int)text_defaults.recent_colors.size() < TextSettings::MAX_RECENT_COLORS) text_defaults.recent_colors.push_back((int)strtol(text_string(colon + 1).c_str(), nullptr, 16) & 0xFFFFFF); }
		}
		fclose(fd);
		if (map_zoom.max_width_km < map_zoom.min_width_km)
			map_zoom.max_width_km = map_zoom.min_width_km;
		text_defaults.clamp_box();
	}

	bool save() const {
		FILE* fd = fopen(file_path(true).c_str(), "w");
		if (fd == nullptr)
			return false;
		fprintf(fd, "climb_filter:%d\n", climb_filter.type);
		fprintf(fd, "climb_window_s:%f\n", climb_filter.window_seconds);
		fprintf(fd, "climb_tau_s:%f\n", climb_filter.time_constant_seconds);
		fprintf(fd, "climb_color_range:%f\n", climb_color_range);
		fprintf(fd, "map_dim:%f\n", map_dim);
		fprintf(fd, "trace_recent_px:%f\n", map_trace.recent_width_px);
		fprintf(fd, "trace_old_px:%f\n", map_trace.old_width_px);
		fprintf(fd, "trace_recent_s:%f\n", map_trace.recent_seconds);
		fprintf(fd, "zoom_min_km:%f\n", map_zoom.min_width_km);
		fprintf(fd, "zoom_max_km:%f\n", map_zoom.max_width_km);
		fprintf(fd, "zoom_transition_s:%f\n", map_zoom.transition_seconds);
		fprintf(fd, "summary_s:%f\n", summary.seconds);
		fprintf(fd, "summary_xc:%d\n", summary.show_xc ? 1 : 0);
		fprintf(fd, "window_w:%d\n", window.width);
		fprintf(fd, "window_h:%d\n", window.height);
		fprintf(fd, "window_maximized:%d\n", window.maximized ? 1 : 0);
		fprintf(fd, "window_panel_w:%d\n", window.panel_width);
		fprintf(fd, "window_timeline_h:%d\n", window.timeline_height);
		fprintf(fd, "sound_volume:%f\n", sound.volume);
		fprintf(fd, "sound_muted:%d\n", sound.muted ? 1 : 0);
		fprintf(fd, "text_x:%f\n", text_defaults.x);
		fprintf(fd, "text_y:%f\n", text_defaults.y);
		fprintf(fd, "text_w:%f\n", text_defaults.w);
		fprintf(fd, "text_h:%f\n", text_defaults.h);
		fprintf(fd, "text_font:%d\n", text_defaults.style.font);
		fprintf(fd, "text_size:%f\n", text_defaults.style.size);
		fprintf(fd, "text_bold:%d\n", text_defaults.style.bold ? 1 : 0);
		fprintf(fd, "text_italic:%d\n", text_defaults.style.italic ? 1 : 0);
		fprintf(fd, "text_strike:%d\n", text_defaults.style.strike ? 1 : 0);
		fprintf(fd, "text_align:%d\n", text_defaults.style.align);
		fprintf(fd, "text_color:%06x\n", TextSettings::pack_color(text_defaults.style.color));
		fprintf(fd, "text_shadow_color:%06x\n", TextSettings::pack_color(text_defaults.style.shadow_color));
		fprintf(fd, "text_shadow_opacity:%f\n", text_defaults.style.shadow_opacity);
		fprintf(fd, "text_shadow_dx:%f\n", text_defaults.style.shadow_dx);
		fprintf(fd, "text_shadow_dy:%f\n", text_defaults.style.shadow_dy);
		fprintf(fd, "text_fade_in_s:%f\n", text_defaults.fade_in_seconds);
		fprintf(fd, "text_dwell_s:%f\n", text_defaults.dwell_seconds);
		fprintf(fd, "text_fade_out_s:%f\n", text_defaults.fade_out_seconds);
		for (size_t i = 0; i < text_defaults.recent_colors.size(); i++)
			fprintf(fd, "text_recent_color:%06x\n", text_defaults.recent_colors[i]);
		fprintf(fd, "last_project:%s\n", last_project.c_str());
		for (int i = 0; i < recent_projects.count(); i++)
			fprintf(fd, "recent_project:%s\n", recent_projects.path(i).c_str());
		fclose(fd);
		return true;
	}
};
