#pragma once

#include <string>

class ProjectFileManager {
public:
	ProjectFileManager();
	~ProjectFileManager();

	void get_project();
	void save_project();

	// Getters;
	const std::string& get_video_file_path() { return m_video_file_path; }
	const std::string& get_telemetry_file_path() { return m_telemetry_file_path; }
	const std::string& get_project_file_path() { return m_project_file_path; }
	float get_launch_time() { return m_launch_time; }
	float get_telemetry_offset() { return m_telemetry_offset; }
	float get_window_start_elapsed() { return m_window_start_elapsed; }
	float get_rotation_angle() { return m_rotation_angle; }

	// Setters;
	void set_launch_time(float t) { m_launch_time = t; }
	void set_telemetry_offset(float offset) { m_telemetry_offset = offset; }
	void set_window_start_elapsed(float t) { m_window_start_elapsed = t; }
	void set_rotation_angle(float r) { m_rotation_angle = r; }

private:
	std::string  m_project_file_path;

	std::string  m_video_file_path;
	std::string  m_telemetry_file_path;
	float        m_launch_time;           // Represents the contents of the file, not necessarily current config.
	float        m_telemetry_offset;      // Represents the contents of the file, not necessarily current config.
	float        m_window_start_elapsed;  // Represents the contents of the file, not necessarily current config.
	float        m_rotation_angle;       // Represents the contents of the file, not necessarily current config.
};