#include "project_data.h"

#include <math.h>
#include <memory>

#include <json/json.h>

#include "file_io.h"

namespace project_io {

namespace {

const char* FORMAT_NAME = "cambot-project";

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
	if (!read_string(root, "telemetry_file", &result.telemetry_file, error)) return false;
	if (!read_number(root, "telemetry_offset_s", &result.telemetry_offset_s, error)) return false;
	if (!read_number(root, "launch_time_s", &result.launch_time_s, error)) return false;
	if (!read_number(root, "window_start_elapsed_s", &result.window_start_elapsed_s, error)) return false;
	if (!read_number(root, "rotation_rad", &result.rotation_rad, error)) return false;

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

	if (result.video_files.empty()) {
		set_error(error, "The project file names no video files.");
		return false;
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
	root["telemetry_file"] = project.telemetry_file;
	root["telemetry_offset_s"] = (double)project.telemetry_offset_s;
	root["launch_time_s"] = (double)project.launch_time_s;
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
