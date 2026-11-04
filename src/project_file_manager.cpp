#include "project_file_manager.h"

#include "file_io.h"
#include "media_order.h"
#include "util.h"

namespace {

const wchar_t* VIDEO_EXTENSIONS = L"mp4;mov;m4v;mkv;avi;mts";
const wchar_t* TELEMETRY_EXTENSIONS = L"telem;log";

std::wstring widen_ascii(const std::string& text) {
	std::wstring wide;
	for (size_t i = 0; i < text.size(); i++)
		wide += (wchar_t)(unsigned char)text[i];
	return wide;
}

}  // namespace

ProjectFileManager::ProjectFileManager() {
}

ProjectFileManager::~ProjectFileManager() {
}

bool ProjectFileManager::open_interactive() {
	// The .ffsw project file may or may not exist yet: the user can either pick an
	// existing one to load, or type the name of a new one to create. must_exist=false is
	// what allows that second case -- a path that doesn't exist yet is fine here.
	std::string path = ffsw::file_dialog(L"ffsw", L"Select or Name a CamBot Project File (*.ffsw)", false);
	if (path.empty())
		return false;  // Cancelled.

	m_project_file_path = path;
	if (fileio::file_exists(path))
		return open_existing(path);
	return create_new();
}

bool ProjectFileManager::open_path(const std::string& path) {
	if (path.empty() || !fileio::file_exists(path))
		return false;
	m_project_file_path = path;
	return open_existing(path);
}

std::string ProjectFileManager::ask_for_telemetry_file() {
	return ffsw::file_dialog(TELEMETRY_EXTENSIONS, L"Locate Telemetry File (*.telem, *.log)");
}

bool ProjectFileManager::create_new_interactive() {
	// Same dialog as the one that opens a project, but here only a name is wanted: one that is
	// already a project is not what was asked for, and is not replaced.
	std::string path = ffsw::file_dialog(L"ffsw", L"Name the New CamBot Project (*.ffsw)", false);
	if (path.empty())
		return false;  // Cancelled.
	if (fileio::file_exists(path)) {
		ffsw::message_box("CamBot - New Project", "\"" + fileio::base_name(path) + "\" is already a project.\n\nUse File > Open Project to open it, or give the new project another name.");
		return false;
	}
	m_project_file_path = path;
	return create_new();
}

bool ProjectFileManager::create_new() {
	// A new project: its inputs are the video file(s) and, if there is one, the telemetry log.
	std::vector<std::string> videos = ffsw::file_dialog_multi(VIDEO_EXTENSIONS,
		L"Select the Video File(s) of the Flight (select all the pieces if the camera split it)");
	if (videos.empty())
		return false;
	media_order::order_files(&videos);

	// The order is a guess from the file names; Source Material in the left panel shows it, and is
	// where it gets fixed if the guess was wrong.

	// The telemetry log is optional: a flight can be a video and nothing else (the log can be added later).
	std::string telemetry;
	ffsw::Answer have_telemetry = ffsw::yes_no_cancel_box("CamBot - Telemetry",
		"Is there a telemetry file for this flight?\n\nYes: choose it now.\nNo: make the project with the video alone. (A telemetry file can be added later, from Source Material in the left panel.)\nCancel: stop, and don't make a project.");
	if (have_telemetry == ffsw::ANSWER_CANCEL)
		return false;
	if (have_telemetry == ffsw::ANSWER_YES) {
		telemetry = ask_for_telemetry_file();
		if (telemetry.empty())
			return false;
	}

	m_data = ProjectData();
	m_data.video_files = videos;
	m_data.telemetry_file = telemetry;
	return save_project();
}

bool ProjectFileManager::open_existing(const std::string& path) {
	ProjectData loaded;
	std::string error;
	if (!project_io::load(path, &loaded, &error)) {
		ffsw::message_box("CamBot - Cannot Open Project", "\"" + fileio::base_name(path) + "\" could not be opened.\n\n" + error);
		return false;
	}
	m_saved_data = loaded;
	m_data = loaded;
	m_autosaved_data = loaded;

	// A recovery file newer than the project means the last session ended without saving
	// (a crash, or the power going).
	bool restored = false;
	std::string recovery = recovery_path();
	if (fileio::file_exists(recovery) && fileio::modified_time(recovery) > fileio::modified_time(path)) {
		ProjectData recovered;
		if (project_io::load(recovery, &recovered, &error) && recovered != loaded) {
			if (ffsw::confirm_box("CamBot - Recover Unsaved Changes",
				"CamBot closed without saving \"" + fileio::base_name(path) + "\" last time, and kept a copy of the changes made after the last save.\n\n"
				"Restore those changes?")) {
				m_data = recovered;
				m_autosaved_data = recovered;
				restored = true;
			} else {
				fileio::delete_file(recovery);
			}
		} else {
			fileio::delete_file(recovery);
		}
	}

	bool changed = false;
	if (!resolve_missing_files(&changed))
		return false;

	// Repairing moved-file paths is housekeeping, not an edit: write it out right away, unless
	// there are restored changes waiting for the user to decide about.
	if (changed && !restored)
		save_project();
	return true;
}

// Asks the user to find a file that is no longer where the project says. Other files from the
// same place are likely to have moved with it, so a folder the user has pointed to is searched
// first next time.
bool ProjectFileManager::locate_file(std::string* path, const std::string& kind, const wchar_t* extensions,
	const std::string& search_directory, std::string* last_found_directory) {
	std::string name = fileio::base_name(*path);

	// Same file name beside the project file, or in the folder the last missing file turned up in.
	std::string candidate = fileio::join(search_directory, name);
	if (!search_directory.empty() && fileio::file_exists(candidate)) {
		*path = candidate;
		return true;
	}
	if (!last_found_directory->empty()) {
		candidate = fileio::join(*last_found_directory, name);
		if (fileio::file_exists(candidate)) {
			*path = candidate;
			return true;
		}
	}

	if (!ffsw::confirm_box("CamBot - File Not Found",
		"CamBot can't find the " + kind + " file this project uses:\n\n" + *path + "\n\nWould you like to locate it?"))
		return false;
	std::wstring title = L"Locate " + widen_ascii(kind) + L" File: " + widen_ascii(name);
	std::string found = ffsw::file_dialog(extensions, title.c_str());
	if (found.empty())
		return false;
	*path = found;
	*last_found_directory = fileio::directory_of(found);
	return true;
}

bool ProjectFileManager::resolve_missing_files(bool* changed) {
	*changed = false;
	std::string project_directory = fileio::directory_of(m_project_file_path);
	std::string last_found_directory;

	for (size_t i = 0; i < m_data.video_files.size(); i++) {
		if (fileio::file_exists(m_data.video_files[i]))
			continue;
		if (!locate_file(&m_data.video_files[i], "video", VIDEO_EXTENSIONS, project_directory, &last_found_directory))
			return false;
		*changed = true;
	}
	if (!m_data.telemetry_file.empty() && !fileio::file_exists(m_data.telemetry_file)) {
		if (!locate_file(&m_data.telemetry_file, "telemetry", TELEMETRY_EXTENSIONS, project_directory, &last_found_directory))
			return false;
		*changed = true;
	}
	return true;
}

bool ProjectFileManager::save_project() {
	if (m_project_file_path.empty())
		return false;
	std::string error;
	if (!project_io::save(m_project_file_path, m_data, &error)) {
		ffsw::message_box("CamBot - Cannot Save Project", "The project could not be saved.\n\n" + error +
			"\n\nYour previous project file has not been changed, and your changes are still open in CamBot. Try saving again.");
		return false;
	}
	m_saved_data = m_data;
	// The saved file now has everything, so the recovery copy is out of date.
	fileio::delete_file(recovery_path());
	m_autosaved_data = m_data;
	return true;
}

void ProjectFileManager::adopt_model_state(const ProjectData& normalized) {
	const bool was_saved = m_data == m_saved_data;
	const bool was_autosaved = m_data == m_autosaved_data;
	m_data = normalized;
	if (was_saved)
		m_saved_data = normalized;
	if (was_autosaved)
		m_autosaved_data = normalized;
}

void ProjectFileManager::autosave_if_due(float now_seconds) {
	if (m_project_file_path.empty())
		return;
	if (m_data == m_saved_data) {
		// Back to exactly what's saved (say, a nudge undone by hand): a recovery copy of the
		// in-between state would only mislead the next session.
		if (m_autosaved_data != m_saved_data) {
			fileio::delete_file(recovery_path());
			m_autosaved_data = m_saved_data;
		}
		return;
	}
	if (m_data == m_autosaved_data)
		return;  // The recovery file is already up to date.
	if (now_seconds - m_last_autosave_seconds < 60.0f)
		return;  // At most once a minute.
	m_last_autosave_seconds = now_seconds;
	std::string error;
	if (project_io::save(recovery_path(), m_data, &error)) {
		m_autosaved_data = m_data;
	} else {
		printf("Could not write the recovery file: %s\n", error.c_str());
	}
}

void ProjectFileManager::discard_recovery() {
	if (!m_project_file_path.empty())
		fileio::delete_file(recovery_path());
}
