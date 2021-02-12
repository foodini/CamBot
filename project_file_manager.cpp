#include "project_file_manager.h"
#include "util.h"

ProjectFileManager::ProjectFileManager() :
	m_video_file_path(""),
	m_telemetry_file_path(""),
	m_launch_time(0.0),
	m_telemetry_offset(0.0)
{
	get_project();
}

ProjectFileManager::~ProjectFileManager() {

}

void ProjectFileManager::get_project() {
	m_project_file_path = ffsw::file_dialog(L"ffsw");

	if (m_project_file_path == "") {
		exit(1);
	}

	FILE* project_fd = fopen(m_project_file_path.c_str(), "r");
	if (project_fd == NULL) {
		m_video_file_path = ffsw::file_dialog(L"mp4");
		m_telemetry_file_path = ffsw::file_dialog(L"telem;log");
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
				m_telemetry_offset = atof(line + 6);
			}
			else if (strncmp(line, "d_lau", 3) == 0) {
				m_launch_time = atof(line + 6);
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

	fclose(project_fd);
}