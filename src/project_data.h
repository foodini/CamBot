#pragma once

// Everything a CamBot project file stores, as plain data, plus reading and writing it.
//
// Project files are JSON (format "cambot-project", version 2).

#include <string>
#include <vector>

#include "altitude_reference.h"

struct ProjectData {
	std::vector<std::string> video_files;   // In playback order; together they are one flight.
	std::string              telemetry_file;
	float                    telemetry_offset_s;
	float                    launch_time_s;
	float                    window_start_elapsed_s;
	float                    rotation_rad;
	AltitudeReferences       altitude_refs;

	ProjectData() : telemetry_offset_s(0.0f), launch_time_s(0.0f), window_start_elapsed_s(0.0f), rotation_rad(0.0f) {}

	// Used to tell whether the project has changed since it was saved. The numbers only ever
	// change when the user changes them, so comparing floats exactly is what's wanted here.
	bool operator==(const ProjectData& other) const {
		return video_files == other.video_files && telemetry_file == other.telemetry_file &&
			telemetry_offset_s == other.telemetry_offset_s && launch_time_s == other.launch_time_s &&
			window_start_elapsed_s == other.window_start_elapsed_s && rotation_rad == other.rotation_rad &&
			altitude_refs == other.altitude_refs;
	}
	bool operator!=(const ProjectData& other) const { return !(*this == other); }
};

namespace project_io {

const int CURRENT_VERSION = 2;

bool parse_json(const std::string& text, ProjectData* project, std::string* error);
std::string to_json(const ProjectData& project);

bool load(const std::string& path, ProjectData* project, std::string* error);

// Writes the JSON format (atomically).
bool save(const std::string& path, const ProjectData& project, std::string* error);

}  // namespace project_io
