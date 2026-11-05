#pragma once

// Everything a CamBot project file stores, as plain data, plus reading and writing it.
//
// Project files are JSON (format "cambot-project", version 4; versions 2 and 3 are still read). The editing
// parts of a project (the library, the capture order, the clips, the sync anchors, the text boxes) are described
// in edit_model.h and text_box.h;
// times in them are ticks, the video time base of the first video file, which is stored beside
// them so the project still reads right if that file is ever replaced by one with another time
// base.

#include <stdint.h>
#include <string>
#include <vector>

#include "altitude_reference.h"
#include "edit_model.h"

struct ProjectData {
	std::vector<std::string> video_files;     // The library: every media file, in the order they were added.
	int                      time_base_num;   // One tick is time_base_num / time_base_den seconds.
	int                      time_base_den;   // (0 until the files have been opened once.)
	std::vector<int>         capture_order;   // Indices into video_files, in the order they were recorded. Empty: as listed.
	std::vector<Clip>        clips;           // The edit timeline. Empty: every file whole, in capture order.
	bool                     edit_started;    // The user has changed the clips by hand.
	std::vector<SyncAnchor>  sync_anchors;    // What ties the footage to the telemetry; see edit_model.h.
	FileTime                 launch_mark;     // Where the user marked launch. media < 0: not marked yet.
	std::vector<TextBox>     text_boxes;      // The text laid over the video; see text_box.h.
	std::string              telemetry_file;
	float                    window_start_elapsed_s;
	float                    rotation_rad;
	AltitudeReferences       altitude_refs;

	// Only when the project was read from a version 2 file, which kept one telemetry offset and
	// the launch time as plain seconds of the flight. apply_to_model() turns them into anchors
	// and a launch mark and fill_from_model() clears them.
	bool                     has_version2_sync;
	float                    version2_offset_s;
	float                    version2_launch_s;

	ProjectData() : time_base_num(0), time_base_den(0), edit_started(false), launch_mark(-1, 0), window_start_elapsed_s(0.0f), rotation_rad(0.0f),
		has_version2_sync(false), version2_offset_s(0.0f), version2_launch_s(0.0f) {}

	// Used to tell whether the project has changed since it was saved. The numbers only ever
	// change when the user changes them, so comparing floats exactly is what's wanted here.
	bool operator==(const ProjectData& other) const {
		return video_files == other.video_files && time_base_num == other.time_base_num && time_base_den == other.time_base_den &&
			capture_order == other.capture_order && clips == other.clips && edit_started == other.edit_started &&
			sync_anchors == other.sync_anchors && launch_mark == other.launch_mark && text_boxes == other.text_boxes &&
			telemetry_file == other.telemetry_file &&
			window_start_elapsed_s == other.window_start_elapsed_s && rotation_rad == other.rotation_rad &&
			altitude_refs == other.altitude_refs && has_version2_sync == other.has_version2_sync &&
			version2_offset_s == other.version2_offset_s && version2_launch_s == other.version2_launch_s;
	}
	bool operator!=(const ProjectData& other) const { return !(*this == other); }
};

namespace project_io {

const int CURRENT_VERSION = 4;

bool parse_json(const std::string& text, ProjectData* project, std::string* error);
std::string to_json(const ProjectData& project);

bool load(const std::string& path, ProjectData* project, std::string* error);

// Writes the JSON format (atomically).
bool save(const std::string& path, const ProjectData& project, std::string* error);

// Sets up `model` from the project, once the files have been opened and their lengths are known
// (`media_durations`, in ticks of tb_num/tb_den seconds, one per file in video_files). Anything
// the project leaves out gets its default (the clips are the files whole, in capture order). False,
// with a sentence for the user, if the project doesn't fit the files, for instance a clip that runs
// past the end of a file that has since been replaced by a shorter one.
bool apply_to_model(const ProjectData& project, const std::vector<int64_t>& media_durations, int tb_num, int tb_den,
	EditModel* model, std::string* error);

// The other way: copies the editing state of `model` into `project`, leaving the rest of the
// project (telemetry file, rotation and so on) alone. The result is how the project is written, so
// applying it again gives back the same model.
void fill_from_model(const EditModel& model, int tb_num, int tb_den, ProjectData* project);

}  // namespace project_io
