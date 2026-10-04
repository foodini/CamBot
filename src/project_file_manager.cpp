#include "project_file_manager.h"
#include "util.h"

ProjectFileManager::ProjectFileManager() :
	m_video_file_path(""),
	m_telemetry_file_path(""),
	m_launch_time(0.0),
	m_telemetry_offset(0.0),
	m_window_start_elapsed(0.0),
	m_rotation_angle(0.0)
{
	get_project();
}

ProjectFileManager::~ProjectFileManager() {

}

void ProjectFileManager::get_project() {
	// The .ffsw project file may or may not exist yet: the user can either pick an
	// existing one to load, or type the name of a new one to create. must_exist=false is
	// what allows that second case -- a path that doesn't exist yet is fine here.
	m_project_file_path = ffsw::file_dialog(L"ffsw", L"Select or Name a CamBot Project File (*.ffsw)", false);

	if (m_project_file_path == "") {
		exit(1);
	}

	FILE* project_fd = fopen(m_project_file_path.c_str(), "r");
	if (project_fd == NULL) {
		// No project file exists at that path -- this is a brand new project. Gather its two
		// real inputs from the user and write the .ffsw file out so it actually exists on disk
		// (and can just be reloaded) next time.
		m_video_file_path = ffsw::file_dialog(L"mp4", L"Locate GoPro Video File (*.mp4)");
		m_telemetry_file_path = ffsw::file_dialog(L"telem;log", L"Locate Telemetry File (*.telem, *.log)");

		if (m_video_file_path == "" || m_telemetry_file_path == "") {
			exit(1);
		}

		m_launch_time = 0.0f;
		m_telemetry_offset = 0.0f;
		m_window_start_elapsed = 0.0f;
		m_rotation_angle = 0.0f;
		save_project();
	} else {
		char line[1024];
		while (!feof(project_fd)) {
			fgets(line, 1023, project_fd);
			line[strlen(line)-1] = '\0'; // kill the \n
			if (strncmp(line, "video", 3) == 0) {
				m_video_file_path = std::string(line + 6);
			}
			else if (strncmp(line, "telem", 3) == 0) {
				m_telemetry_file_path = std::string(line + 6);
			}
			else if (strncmp(line, "d_tel", 3) == 0) {
				m_telemetry_offset = (float)atof(line + 6);
			}
			else if (strncmp(line, "d_lau", 3) == 0) {
				m_launch_time = (float)atof(line + 6);
			}
			else if (strncmp(line, "d_win", 3) == 0) {
				m_window_start_elapsed = (float)atof(line + 6);
			}
			else if (strncmp(line, "d_rot", 3) == 0) {
				m_rotation_angle = (float)atof(line + 6);
			}
		}
		fclose(project_fd);
	}
}

void ProjectFileManager::save_project() {
	FILE* project_fd = fopen(m_project_file_path.c_str(), "w");

	// I should do a sanity check...

	// If this ever has to be more complicated, I'll make it more complicated. Until then,
	// I'm not pulling in a json or yaml builder & interpreter.
	fprintf(project_fd, "video:%s\n", m_video_file_path.c_str());
	fprintf(project_fd, "telem:%s\n", m_telemetry_file_path.c_str());
	fprintf(project_fd, "d_tel:%f\n", m_telemetry_offset);
	fprintf(project_fd, "d_lau:%f\n", m_launch_time);
	fprintf(project_fd, "d_win:%f\n", m_window_start_elapsed);
	fprintf(project_fd, "d_rot:%f\n", m_rotation_angle);

	fclose(project_fd);
}