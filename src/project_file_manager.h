#pragma once

#include <string>
#include <vector>

#include "altitude_reference.h"
#include "project_data.h"

// Finds, loads and saves the project file (.ffsw, JSON; see project_data.h) and holds what's in
// it. The files it names are checked when the project is opened, so the rest of the program can
// count on them existing.
class ProjectFileManager {
public:
	ProjectFileManager();
	~ProjectFileManager();

	// Asks the user for a project file, either an existing one or the name of a new one (and, for
	// a new one, the video files and the telemetry file that go in it). Returns true once there is
	// a project to work on. Returns false if the user cancelled or something was wrong; in the
	// second case they have already been told what.
	bool open_interactive();

	// Opens the existing project file at `path` without asking which. False if there is no such
	// file or it could not be opened (in which case the user has been told what was wrong, unless
	// there was no such file); the manager is then not to be used until another open succeeds.
	bool open_path(const std::string& path);

	// Makes a new project: asks for the project's name, then its video files and telemetry file,
	// and writes the project file. False if the user cancelled or something was wrong (they have
	// been told what). A name that is already a project is refused, not overwritten.
	bool create_new_interactive();

	// Writes the project file, and clears the unsaved-changes state (and the recovery file, see
	// below). On failure the user is told, and false comes back.
	bool save_project();

	// The project is only ever written when the user asks (File > Save, Ctrl+S, or "Yes" when
	// closing with changes). So that a crash doesn't cost the work since the last save, while
	// there are unsaved changes a copy goes to "<project>.autosave" at most once a minute. That
	// file never replaces the project: opening a project that has a newer one offers to restore
	// it, and a normal exit deletes it.
	//
	// True if the project differs from what was last saved (or restored from).
	bool has_unsaved_changes() const { return m_data != m_saved_data; }
	// Call every frame with the time in seconds; writes the recovery file when it's due.
	void autosave_if_due(float now_seconds);
	// Removes the recovery file, for a normal exit.
	void discard_recovery();

	// Getters;
	const std::vector<std::string>& get_video_files() const { return m_data.video_files; }
	const std::string& get_telemetry_file_path() const { return m_data.telemetry_file; }
	const std::string& get_project_file_path() const { return m_project_file_path; }
	float get_window_start_elapsed() const { return m_data.window_start_elapsed_s; }
	float get_rotation_angle() const { return m_data.rotation_rad; }
	const AltitudeReferences& get_altitude_references() const { return m_data.altitude_refs; }

	// The whole project as read, for building the editing model from (see project_io::apply_to_model),
	// and the live copy to write the model's state into (see project_io::fill_from_model). The copy
	// is only a mirror: the model is the truth, and EnvConfig refreshes the copy every frame.
	const ProjectData& get_data() const { return m_data; }
	ProjectData*       mutable_data()   { return &m_data; }
	// Call once, right after the model has been built from the project and mirrored back into the
	// copy: `normalized` is what that gives. Reading a project and writing it back out can change
	// how it is spelled (an older format, defaults filled in) without the user changing anything, so
	// that is not counted as an unsaved change.
	void adopt_model_state(const ProjectData& normalized);

	// Setters;
	void set_window_start_elapsed(float t) { m_data.window_start_elapsed_s = t; }
	void set_rotation_angle(float r) { m_data.rotation_rad = r; }
	void set_altitude_references(const AltitudeReferences& refs) { m_data.altitude_refs = refs; }

private:
	bool open_existing(const std::string& path);
	bool create_new();
	// Makes sure every file the project names exists, offering to look for or locate any that
	// moved. *changed says whether any path was altered.
	bool resolve_missing_files(bool* changed);
	bool locate_file(std::string* path, const std::string& kind, const wchar_t* extensions, const std::string& search_directory,
		std::string* last_found_directory);

	std::string  m_project_file_path;
	ProjectData  m_data;   // The contents of the file, not necessarily the current config.
	ProjectData  m_saved_data;       // What the project file on disk holds, as far as we know.
	ProjectData  m_autosaved_data;   // What the recovery file holds (or m_saved_data if there is none).
	float        m_last_autosave_seconds = -1000.0f;
	std::string  recovery_path() const { return m_project_file_path + ".autosave"; }
};
