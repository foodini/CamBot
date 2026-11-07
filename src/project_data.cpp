#include "project_data.h"

#include <math.h>
#include <memory>

#include <json/json.h>

#include "file_io.h"

namespace project_io {

namespace {

const char* FORMAT_NAME = "cambot-project";
const int OLDEST_READABLE_VERSION = 2;

void set_error(std::string* error, const std::string& message) {
	if (error)
		*error = message;
}

// Reads an optional number field. A missing field leaves *value alone; a field of the wrong
// type is an error.
bool read_number(const Json::Value& object, const char* key, float* value, std::string* error) {
	if (!object.isMember(key))
		return true;
	const Json::Value& field = object[key];
	if (!field.isNumeric()) {
		set_error(error, std::string("The project file's \"") + key + "\" should be a number.");
		return false;
	}
	*value = (float)field.asDouble();
	return true;
}

bool read_string(const Json::Value& object, const char* key, std::string* value, std::string* error) {
	if (!object.isMember(key))
		return true;
	const Json::Value& field = object[key];
	if (!field.isString()) {
		set_error(error, std::string("The project file's \"") + key + "\" should be text.");
		return false;
	}
	*value = field.asString();
	return true;
}

// A required whole number of at least `minimum` inside an object that is part of a list.
bool read_whole(const Json::Value& object, const char* key, int64_t minimum, int64_t* value, std::string* error) {
	if (!object.isObject() || !object.isMember(key) || !object[key].isIntegral()) {
		set_error(error, std::string("The project file has an entry without a whole number \"") + key + "\".");
		return false;
	}
	int64_t number = (int64_t)object[key].asInt64();
	if (number < minimum) {
		set_error(error, std::string("The project file has a \"") + key + "\" that is out of range.");
		return false;
	}
	*value = number;
	return true;
}

const char* audio_mode_name(int mode) {
	if (mode == AUDIO_CHANGE_PITCH)
		return "change_pitch";
	if (mode == AUDIO_MUTE)
		return "mute";
	return "keep_pitch";
}

// Reads a clip's optional speed and what to do with its sound at that speed (project version 5; older projects have neither,
// which means normal speed).
bool read_clip_speed(const Json::Value& object, double* speed, int* audio_mode, std::string* error) {
	*speed = 1.0;
	*audio_mode = AUDIO_KEEP_PITCH;
	if (object.isMember("speed")) {
		if (!object["speed"].isNumeric()) {
			set_error(error, "The project file has a clip whose \"speed\" is not a number.");
			return false;
		}
		const double value = object["speed"].asDouble();
		if (!(value >= MIN_SPEED && value <= MAX_SPEED)) {
			set_error(error, "The project file has a clip whose \"speed\" is outside what CamBot can play (a tenth of normal speed to sixteen times).");
			return false;
		}
		*speed = value;
	}
	if (object.isMember("audio")) {
		if (!object["audio"].isString()) {
			set_error(error, "The project file has a clip whose \"audio\" should be a name.");
			return false;
		}
		const std::string name = object["audio"].asString();
		if (name == "keep_pitch")
			*audio_mode = AUDIO_KEEP_PITCH;
		else if (name == "change_pitch")
			*audio_mode = AUDIO_CHANGE_PITCH;
		else if (name == "mute")
			*audio_mode = AUDIO_MUTE;
		else {
			set_error(error, "The project file has a clip whose \"audio\" is not one of keep_pitch, change_pitch or mute.");
			return false;
		}
	}
	return true;
}

const char* transition_type_name(int type) {
	if (type == TRANSITION_CROSSFADE)
		return "crossfade";
	if (type == TRANSITION_FADE)
		return "fade";
	return "cut";
}

// Reads the list of transitions that aren't cuts (project version 6), each naming its seam (see EditList), into a list with one
// entry for every seam of the `clip_count` clips.
bool read_transitions(const Json::Value& list, size_t clip_count, std::vector<Transition>* transitions, std::string* error) {
	if (!list.isArray()) {
		set_error(error, "The project file's \"transitions\" should be a list.");
		return false;
	}
	transitions->assign(clip_count + 1, Transition());
	for (Json::ArrayIndex i = 0; i < list.size(); i++) {
		int64_t seam = 0;
		int64_t ticks = 0;
		if (!read_whole(list[i], "seam", 0, &seam, error) || !read_whole(list[i], "ticks", 0, &ticks, error))
			return false;
		if (seam > (int64_t)clip_count) {
			set_error(error, "The project file has a transition at a place that is not in the edit.");
			return false;
		}
		if (!list[i].isMember("type") || !list[i]["type"].isString()) {
			set_error(error, "The project file has a transition without a \"type\".");
			return false;
		}
		const std::string name = list[i]["type"].asString();
		int type = TRANSITION_CUT;
		if (name == "fade")
			type = TRANSITION_FADE;
		else if (name == "crossfade")
			type = TRANSITION_CROSSFADE;
		else if (name != "cut") {
			set_error(error, "The project file has a transition whose \"type\" is not one of cut, fade or crossfade.");
			return false;
		}
		(*transitions)[(size_t)seam] = Transition(type, ticks);
	}
	return true;
}

bool read_media_index(const Json::Value& object, size_t media_count, int* media, std::string* error) {
	int64_t index = 0;
	if (!read_whole(object, "media", 0, &index, error))
		return false;
	if (index >= (int64_t)media_count) {
		set_error(error, "The project file refers to a video file that is not in its list.");
		return false;
	}
	*media = (int)index;
	return true;
}

bool read_file_time(const Json::Value& object, size_t media_count, FileTime* time, std::string* error) {
	int media = 0;
	int64_t local = 0;
	if (!read_media_index(object, media_count, &media, error) || !read_whole(object, "local", 0, &local, error))
		return false;
	*time = FileTime(media, local);
	return true;
}

// Reads three numbers, 0 to 1, from a list of three in an object (a color).
bool read_color(const Json::Value& object, const char* key, float color[3], std::string* error) {
	if (!object.isMember(key))
		return true;
	const Json::Value& list = object[key];
	if (!list.isArray() || list.size() != 3) {
		set_error(error, std::string("The project file's text \"") + key + "\" should be three numbers.");
		return false;
	}
	float values[3];
	for (Json::ArrayIndex i = 0; i < 3; i++) {
		if (!list[i].isNumeric()) {
			set_error(error, std::string("The project file's text \"") + key + "\" should be three numbers.");
			return false;
		}
		double value = list[i].asDouble();
		values[i] = (float)(value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value));
	}
	for (int i = 0; i < 3; i++)
		color[i] = values[i];
	return true;
}

Json::Value color_to_json(const float color[3]) {
	Json::Value list(Json::arrayValue);
	for (int i = 0; i < 3; i++)
		list.append((double)color[i]);
	return list;
}

bool read_text_style(const Json::Value& object, TextStyle* style, std::string* error) {
	if (!object.isObject()) {
		set_error(error, "The project file's text \"style\" is not in the expected form.");
		return false;
	}
	int64_t whole = 0;
	if (object.isMember("font")) {
		if (!read_whole(object, "font", 0, &whole, error))
			return false;
		style->font = (int)whole;
	}
	if (object.isMember("align")) {
		if (!read_whole(object, "align", 0, &whole, error))
			return false;
		if (whole > TEXT_ALIGN_RIGHT) {
			set_error(error, "The project file has a text \"align\" that is out of range.");
			return false;
		}
		style->align = (int)whole;
	}
	if (!read_number(object, "size", &style->size, error))
		return false;
	if (!(style->size > 0.0f && style->size <= 1.0f)) {
		set_error(error, "The project file has a text \"size\" that is out of range.");
		return false;
	}
	const char* flag_names[3] = { "bold", "italic", "strike" };
	bool* flags[3] = { &style->bold, &style->italic, &style->strike };
	for (int i = 0; i < 3; i++) {
		if (!object.isMember(flag_names[i]))
			continue;
		if (!object[flag_names[i]].isBool()) {
			set_error(error, std::string("The project file's text \"") + flag_names[i] + "\" should be true or false.");
			return false;
		}
		*flags[i] = object[flag_names[i]].asBool();
	}
	if (!read_color(object, "color", style->color, error))
		return false;
	if (object.isMember("shadow")) {
		const Json::Value& shadow = object["shadow"];
		if (!shadow.isObject()) {
			set_error(error, "The project file's text \"shadow\" is not in the expected form.");
			return false;
		}
		if (!read_number(shadow, "dx", &style->shadow_dx, error) || !read_number(shadow, "dy", &style->shadow_dy, error) ||
			!read_number(shadow, "opacity", &style->shadow_opacity, error) || !read_color(shadow, "color", style->shadow_color, error))
			return false;
		if (style->shadow_opacity < 0.0f)
			style->shadow_opacity = 0.0f;
		if (style->shadow_opacity > 1.0f)
			style->shadow_opacity = 1.0f;
	}
	return true;
}

bool read_text_boxes(const Json::Value& list, size_t media_count, std::vector<TextBox>* boxes, std::string* error) {
	if (!list.isArray()) {
		set_error(error, "The project file's \"text_boxes\" should be a list.");
		return false;
	}
	for (Json::ArrayIndex i = 0; i < list.size(); i++) {
		const Json::Value& entry = list[i];
		TextBox box;
		int64_t id = 0;
		if (!read_whole(entry, "id", 0, &id, error) || !read_file_time(entry, media_count, &box.anchor, error) ||
			!read_whole(entry, "fade_in", 0, &box.fade_in_ticks, error) || !read_whole(entry, "dwell", 0, &box.dwell_ticks, error) ||
			!read_whole(entry, "fade_out", 0, &box.fade_out_ticks, error))
			return false;
		box.id = (int)id;
		if (!read_string(entry, "text", &box.text, error) || !read_number(entry, "x", &box.x, error) ||
			!read_number(entry, "y", &box.y, error) || !read_number(entry, "w", &box.w, error) || !read_number(entry, "h", &box.h, error))
			return false;
		if (!(box.w > 0.0f && box.h > 0.0f)) {
			set_error(error, "The project file has a text box with no width or height.");
			return false;
		}
		if (entry.isMember("style") && !read_text_style(entry["style"], &box.style, error))
			return false;
		for (size_t other = 0; other < boxes->size(); other++) {
			if ((*boxes)[other].id == box.id) {
				set_error(error, "The project file has two text boxes with the same \"id\".");
				return false;
			}
		}
		boxes->push_back(box);
	}
	return true;
}

Json::Value text_box_to_json(const TextBox& box) {
	Json::Value entry(Json::objectValue);
	entry["id"] = box.id;
	entry["media"] = box.anchor.media;
	entry["local"] = Json::Value((Json::Int64)box.anchor.local_ticks);
	entry["fade_in"] = Json::Value((Json::Int64)box.fade_in_ticks);
	entry["dwell"] = Json::Value((Json::Int64)box.dwell_ticks);
	entry["fade_out"] = Json::Value((Json::Int64)box.fade_out_ticks);
	entry["text"] = box.text;
	entry["x"] = (double)box.x;
	entry["y"] = (double)box.y;
	entry["w"] = (double)box.w;
	entry["h"] = (double)box.h;
	Json::Value style(Json::objectValue);
	style["font"] = box.style.font;
	style["size"] = (double)box.style.size;
	style["bold"] = box.style.bold;
	style["italic"] = box.style.italic;
	style["strike"] = box.style.strike;
	style["align"] = box.style.align;
	style["color"] = color_to_json(box.style.color);
	Json::Value shadow(Json::objectValue);
	shadow["dx"] = (double)box.style.shadow_dx;
	shadow["dy"] = (double)box.style.shadow_dy;
	shadow["color"] = color_to_json(box.style.shadow_color);
	shadow["opacity"] = (double)box.style.shadow_opacity;
	style["shadow"] = shadow;
	entry["style"] = style;
	return entry;
}

// Reads the parts of the file that describe the library, the clips and the sync (versions 3 and up).
bool read_editing_state(const Json::Value& root, ProjectData* result, std::string* error) {
	const size_t media_count = result->video_files.size();

	if (root.isMember("time_base")) {
		const Json::Value& base = root["time_base"];
		if (!base.isArray() || base.size() != 2 || !base[0].isInt() || !base[1].isInt() || base[0].asInt() <= 0 || base[1].asInt() <= 0) {
			set_error(error, "The project file's \"time_base\" should be two positive whole numbers.");
			return false;
		}
		result->time_base_num = base[0].asInt();
		result->time_base_den = base[1].asInt();
	}

	if (root.isMember("capture_order")) {
		const Json::Value& order = root["capture_order"];
		if (!order.isArray()) {
			set_error(error, "The project file's \"capture_order\" should be a list of numbers.");
			return false;
		}
		for (Json::ArrayIndex i = 0; i < order.size(); i++) {
			if (!order[i].isInt()) {
				set_error(error, "The project file's \"capture_order\" should be a list of numbers.");
				return false;
			}
			result->capture_order.push_back(order[i].asInt());
		}
		if (!CaptureSequence::is_valid_order(result->capture_order, (int)media_count)) {
			set_error(error, "The project file's \"capture_order\" does not list each video file once.");
			return false;
		}
	}

	if (root.isMember("clips")) {
		const Json::Value& clips = root["clips"];
		if (!clips.isArray()) {
			set_error(error, "The project file's \"clips\" should be a list.");
			return false;
		}
		for (Json::ArrayIndex i = 0; i < clips.size(); i++) {
			int media = 0;
			int64_t in_ticks = 0;
			int64_t out_ticks = 0;
			if (!read_media_index(clips[i], media_count, &media, error) || !read_whole(clips[i], "in", 0, &in_ticks, error) ||
				!read_whole(clips[i], "out", 0, &out_ticks, error))
				return false;
			if (out_ticks <= in_ticks) {
				set_error(error, "The project file has a clip that ends before it begins.");
				return false;
			}
			double speed = 1.0;
			int audio_mode = AUDIO_KEEP_PITCH;
			if (!read_clip_speed(clips[i], &speed, &audio_mode, error))
				return false;
			result->clips.push_back(Clip(media, in_ticks, out_ticks, speed, audio_mode));
		}
		if (!result->clips.empty() && result->time_base_num == 0) {
			set_error(error, "The project file has clips but no \"time_base\" to measure them in.");
			return false;
		}
	}

	if (root.isMember("transitions")) {
		if (result->clips.empty()) {
			set_error(error, "The project file has transitions but no clips for them to be between.");
			return false;
		}
		if (!read_transitions(root["transitions"], result->clips.size(), &result->transitions, error))
			return false;
		bool any = false;
		for (size_t i = 0; i < result->transitions.size(); i++) {
			if (!result->transitions[i].is_cut())
				any = true;
		}
		if (!any)
			result->transitions.clear();
	}

	if (root.isMember("edit_started")) {
		if (!root["edit_started"].isBool()) {
			set_error(error, "The project file's \"edit_started\" should be true or false.");
			return false;
		}
		result->edit_started = root["edit_started"].asBool();
	}

	if (root.isMember("sync_anchors")) {
		const Json::Value& anchors = root["sync_anchors"];
		if (!anchors.isArray()) {
			set_error(error, "The project file's \"sync_anchors\" should be a list.");
			return false;
		}
		for (Json::ArrayIndex i = 0; i < anchors.size(); i++) {
			FileTime at;
			if (!read_file_time(anchors[i], media_count, &at, error))
				return false;
			float telemetry_seconds = 0.0f;
			if (!anchors[i].isMember("telemetry_s") || !anchors[i]["telemetry_s"].isNumeric()) {
				set_error(error, "The project file has a sync point without a \"telemetry_s\" number.");
				return false;
			}
			telemetry_seconds = (float)anchors[i]["telemetry_s"].asDouble();
			result->sync_anchors.push_back(SyncAnchor(at, telemetry_seconds));
		}
		if (!result->sync_anchors.empty() && result->time_base_num == 0) {
			set_error(error, "The project file has sync points but no \"time_base\" to measure them in.");
			return false;
		}
	}

	if (root.isMember("launch_mark")) {
		if (!read_file_time(root["launch_mark"], media_count, &result->launch_mark, error))
			return false;
	}

	if (root.isMember("text_boxes")) {
		if (!read_text_boxes(root["text_boxes"], media_count, &result->text_boxes, error))
			return false;
		if (!result->text_boxes.empty() && result->time_base_num == 0) {
			set_error(error, "The project file has text boxes but no \"time_base\" to measure them in.");
			return false;
		}
	}
	return true;
}

}  // namespace

bool parse_json(const std::string& text, ProjectData* project, std::string* error) {
	Json::CharReaderBuilder builder;
	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	Json::Value root;
	std::string parse_errors;
	const char* begin = text.data();
	if (!reader->parse(begin, begin + text.size(), &root, &parse_errors)) {
		set_error(error, "The project file is damaged and could not be read:\n" + parse_errors);
		return false;
	}
	if (!root.isObject()) {
		set_error(error, "This does not look like a CamBot project file.");
		return false;
	}
	if (root.get("format", "").asString() != FORMAT_NAME) {
		set_error(error, "This does not look like a CamBot project file.");
		return false;
	}
	if (!root.isMember("version") || !root["version"].isInt()) {
		set_error(error, "The project file has no version number.");
		return false;
	}
	int version = root["version"].asInt();
	if (version > CURRENT_VERSION) {
		set_error(error, "This project was saved by a newer version of CamBot (project format " + std::to_string(version) +
			"). Please update CamBot to open it.");
		return false;
	}
	if (version < OLDEST_READABLE_VERSION) {
		set_error(error, "This project was saved by a much older version of CamBot (project format " + std::to_string(version) +
			") and can't be read any more. Please make a new project.");
		return false;
	}

	ProjectData result;
	if (root.isMember("video_files")) {
		const Json::Value& files = root["video_files"];
		if (!files.isArray()) {
			set_error(error, "The project file's \"video_files\" should be a list of file names.");
			return false;
		}
		for (Json::ArrayIndex i = 0; i < files.size(); i++) {
			if (!files[i].isString()) {
				set_error(error, "The project file's \"video_files\" should be a list of file names.");
				return false;
			}
			result.video_files.push_back(files[i].asString());
		}
	}
	if (result.video_files.empty()) {
		set_error(error, "The project file names no video files.");
		return false;
	}
	if (!read_string(root, "telemetry_file", &result.telemetry_file, error)) return false;
	if (!read_number(root, "window_start_elapsed_s", &result.window_start_elapsed_s, error)) return false;
	if (!read_number(root, "rotation_rad", &result.rotation_rad, error)) return false;

	if (version == 2) {
		// One offset and the launch time, both as seconds of the whole flight.
		result.has_version2_sync = true;
		if (!read_number(root, "telemetry_offset_s", &result.version2_offset_s, error)) return false;
		if (!read_number(root, "launch_time_s", &result.version2_launch_s, error)) return false;
	} else {
		if (!read_editing_state(root, &result, error))
			return false;
	}

	if (root.isMember("altitude_refs")) {
		const Json::Value& refs = root["altitude_refs"];
		if (!refs.isObject()) {
			set_error(error, "The project file's \"altitude_refs\" is not in the expected form.");
			return false;
		}
		if (!read_number(refs, "start_ft", &result.altitude_refs.start_ft, error)) return false;
		if (!read_number(refs, "end_ft", &result.altitude_refs.end_ft, error)) return false;
		if (refs.isMember("end_same_as_start")) {
			if (!refs["end_same_as_start"].isBool()) {
				set_error(error, "The project file's \"end_same_as_start\" should be true or false.");
				return false;
			}
			result.altitude_refs.end_same_as_start = refs["end_same_as_start"].asBool();
		}
	}

	*project = result;
	return true;
}

std::string to_json(const ProjectData& project) {
	Json::Value root(Json::objectValue);
	root["format"] = FORMAT_NAME;
	root["version"] = CURRENT_VERSION;

	Json::Value files(Json::arrayValue);
	for (size_t i = 0; i < project.video_files.size(); i++)
		files.append(project.video_files[i]);
	root["video_files"] = files;

	if (project.time_base_num > 0 && project.time_base_den > 0) {
		Json::Value base(Json::arrayValue);
		base.append(project.time_base_num);
		base.append(project.time_base_den);
		root["time_base"] = base;
	}
	if (!project.capture_order.empty()) {
		Json::Value order(Json::arrayValue);
		for (size_t i = 0; i < project.capture_order.size(); i++)
			order.append(project.capture_order[i]);
		root["capture_order"] = order;
	}
	if (!project.clips.empty()) {
		Json::Value clips(Json::arrayValue);
		for (size_t i = 0; i < project.clips.size(); i++) {
			Json::Value clip(Json::objectValue);
			clip["media"] = project.clips[i].media;
			clip["in"] = Json::Value((Json::Int64)project.clips[i].in_ticks);
			clip["out"] = Json::Value((Json::Int64)project.clips[i].out_ticks);
			if (project.clips[i].speed != 1.0)
				clip["speed"] = project.clips[i].speed;
			if (project.clips[i].audio_mode != AUDIO_KEEP_PITCH)
				clip["audio"] = audio_mode_name(project.clips[i].audio_mode);
			clips.append(clip);
		}
		root["clips"] = clips;
	}
	if (project.transitions.size() == project.clips.size() + 1) {
		Json::Value transitions(Json::arrayValue);
		for (size_t seam = 0; seam < project.transitions.size(); seam++) {
			if (project.transitions[seam].is_cut())
				continue;
			Json::Value transition(Json::objectValue);
			transition["seam"] = (Json::UInt64)seam;
			transition["type"] = transition_type_name(project.transitions[seam].type);
			transition["ticks"] = Json::Value((Json::Int64)project.transitions[seam].ticks);
			transitions.append(transition);
		}
		if (!transitions.empty())
			root["transitions"] = transitions;
	}
	root["edit_started"] = project.edit_started;
	if (!project.sync_anchors.empty()) {
		Json::Value anchors(Json::arrayValue);
		for (size_t i = 0; i < project.sync_anchors.size(); i++) {
			Json::Value anchor(Json::objectValue);
			anchor["media"] = project.sync_anchors[i].at.media;
			anchor["local"] = Json::Value((Json::Int64)project.sync_anchors[i].at.local_ticks);
			anchor["telemetry_s"] = (double)project.sync_anchors[i].telemetry_seconds;
			anchors.append(anchor);
		}
		root["sync_anchors"] = anchors;
	}
	if (project.launch_mark.media >= 0) {
		Json::Value launch(Json::objectValue);
		launch["media"] = project.launch_mark.media;
		launch["local"] = Json::Value((Json::Int64)project.launch_mark.local_ticks);
		root["launch_mark"] = launch;
	}

	if (!project.text_boxes.empty()) {
		Json::Value boxes(Json::arrayValue);
		for (size_t i = 0; i < project.text_boxes.size(); i++)
			boxes.append(text_box_to_json(project.text_boxes[i]));
		root["text_boxes"] = boxes;
	}

	root["telemetry_file"] = project.telemetry_file;
	root["window_start_elapsed_s"] = (double)project.window_start_elapsed_s;
	root["rotation_rad"] = (double)project.rotation_rad;

	Json::Value refs(Json::objectValue);
	refs["start_ft"] = (double)project.altitude_refs.start_ft;
	refs["end_ft"] = (double)project.altitude_refs.end_ft;
	refs["end_same_as_start"] = project.altitude_refs.end_same_as_start;
	root["altitude_refs"] = refs;

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "  ";
	writer["precision"] = 9;  // Enough to get a float back exactly.
	writer["emitUTF8"] = true;
	return Json::writeString(writer, root) + "\n";
}

bool load(const std::string& path, ProjectData* project, std::string* error) {
	std::string text;
	if (!fileio::read_text(path, &text, error))
		return false;
	// Skip a UTF-8 byte order mark if an editor added one.
	if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
		text.erase(0, 3);
	return parse_json(text, project, error);
}

bool save(const std::string& path, const ProjectData& project, std::string* error) {
	return fileio::write_text_atomic(path, to_json(project), error);
}

}  // namespace project_io
