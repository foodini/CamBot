#include <algorithm>

#include "env_config.h"
#include "media_container_manager.h"
#include "telemetry.h"
#include "util.h"

EnvConfig* EnvConfig::instance = nullptr;

EnvConfig::EnvConfig(MediaContainerMgr* mcm, FontManager* fm, ProjectFileManager* pfm, float screen_width, float screen_height, float ui_height) :
	media_mgr(mcm),
	font_mgr(fm),
	project_file_mgr(pfm),
	m_launch_time(pfm->get_launch_time()),
	m_altitude_refs(pfm->get_altitude_references()),
	m_user_settings(),
	m_screen_width(screen_width),
	m_screen_height(screen_height),
	m_ui_height(ui_height),
	m_screen_to_pixel_space_projection(glm::mat4(1.0f)),
	m_pixel_to_screen_space_projection(glm::mat4(1.0f))
{
	if (instance != nullptr) {
		throw "Cannot create more than one EnvConfig";
	}
	instance = this;

	recompute_projections();
}

float EnvConfig::time_parametric() const {
	return media_mgr->in_parametric();
}

float EnvConfig::media_in_elapsed() const {
	return media_mgr->in_elapsed();
}

float EnvConfig::media_in_duration() const {
	return media_mgr->in_duration();
}

//TODO(2): move to the MediaContainerMgr:
float EnvConfig::media_out_elapsed() const {
	return media_in_elapsed(); //+ some_offset;
}

float EnvConfig::flight_time() const {
	return media_in_elapsed() - m_launch_time;
}

bool EnvConfig::telemetry_offset(float offset) {
	TelemetryMgr::instance->set_offset(offset);
	save_project();
	return true;
}

float EnvConfig::telemetry_offset() const {
	return TelemetryMgr::instance->offset();
}

bool EnvConfig::climb_filter_settings(const ClimbFilterSettings& settings) {
	if (!TelemetryMgr::instance->set_climb_filter_settings(settings))
		return false;
	m_user_settings.climb_filter = settings;
	save_user_settings();
	return true;
}

bool EnvConfig::map_zoom_settings(const MapZoomSettings& settings) {
	if (!TelemetryMgr::instance->set_map_zoom_settings(settings))
		return false;
	m_user_settings.map_zoom = settings;
	save_user_settings();
	return true;
}

float EnvConfig::telemetry_duration() const {
	return TelemetryMgr::instance->duration();
}

float EnvConfig::telemetry_elapsed() const {
	return TelemetryMgr::instance->elapsed_at(media_in_elapsed());
}

bool EnvConfig::launch_time(float launch_time) { 
	m_launch_time = launch_time; 
	// Snapshot telemetry's elapsed time at this launch marker, using whatever offset is in
	// effect right now -- see TelemetryMgr::window_start_elapsed()'s doc comment for why this
	// is a one-time snapshot rather than a live formula.
	TelemetryMgr::instance->set_window_start_elapsed(TelemetryMgr::instance->elapsed_at(launch_time));
	save_project();
	return true;
}

int32_t EnvConfig::telemetry_index() const {
	return TelemetryMgr::instance->index_at(media_in_elapsed());
}

const TelemetrySlice& EnvConfig::telemetry_slice() const {
	return TelemetryMgr::instance->slice_at(media_in_elapsed());
}

float EnvConfig::telemetry_window_start_elapsed() const {
	return TelemetryMgr::instance->window_start_elapsed();
}

int32_t EnvConfig::telemetry_window_start_index() const {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(TelemetryMgr::instance->window_start_elapsed());
	int32_t last_valid = (int32_t)TelemetryMgr::instance->size() - 1;
	if (index < 0) {
		return 0;
	} else if (index > last_valid) {
		return last_valid;
	}
	return index;
}

int32_t EnvConfig::telemetry_window_end_index() const {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(TelemetryMgr::instance->window_start_elapsed() + media_in_duration());
	int32_t last_valid = (int32_t)TelemetryMgr::instance->size() - 1;
	if (index < 0) {
		return 0;
	} else if (index > last_valid) {
		return last_valid;
	}
	return index;
}

int32_t EnvConfig::telemetry_video_start_index() const {
	TelemetryMgr* telemetry = TelemetryMgr::instance;
	int32_t index = telemetry->index_for_elapsed(telemetry->elapsed_at(0.0f));
	int32_t last_valid = (int32_t)telemetry->size() - 1;
	return (std::max)(0, (std::min)(index, last_valid));
}

int32_t EnvConfig::telemetry_video_end_index() const {
	TelemetryMgr* telemetry = TelemetryMgr::instance;
	int32_t index = telemetry->index_for_elapsed(telemetry->elapsed_at(media_in_duration()));
	int32_t last_valid = (int32_t)telemetry->size() - 1;
	return (std::max)(0, (std::min)(index, last_valid));
}

void EnvConfig::sync_altitude_correction() {
	TelemetryMgr* telemetry = TelemetryMgr::instance;
	if (telemetry == nullptr || !telemetry->parsing_done())
		return;
	telemetry->set_altitude_references(m_altitude_refs, telemetry_video_start_index(), telemetry_video_end_index());
}

float EnvConfig::telemetry_distance_flown() const {
	const TelemetrySlice& window_start_slice = (*TelemetryMgr::instance)[telemetry_window_start_index()];
	return telemetry_slice().m_total_distance - window_start_slice.m_total_distance;
}

void EnvConfig::save_project() {
	project_file_mgr->set_launch_time(m_launch_time);
	project_file_mgr->set_telemetry_offset(TelemetryMgr::instance->offset());
	project_file_mgr->set_window_start_elapsed(TelemetryMgr::instance->window_start_elapsed());
	project_file_mgr->set_rotation_angle(media_mgr->rotation_angle());
	project_file_mgr->set_altitude_references(m_altitude_refs);
	project_file_mgr->save_project();
}

void EnvConfig::recompute_projections() {
	float aspect_ratio = m_screen_width / m_screen_height;
	glm::mat4 projection(1.0f);

	glm::mat4 scale = glm::scale(
		glm::mat4(1.0f),
		glm::vec3(2.0 / m_screen_width, 2.0 / m_screen_height, 1.0f)
	);
	projection = scale * projection;

	glm::mat4 xlate = glm::translate(
		glm::mat4(1.0f),
		glm::vec3(-1.0f, -1.0f, 0.0f)
	);
	m_pixel_to_screen_space_projection = xlate * projection;


	projection = glm::mat4(1.0f);
	xlate = glm::translate(
		glm::mat4(1.0f),
		glm::vec3(1.0f, 1.0f, 0.0f)
	);
	projection = xlate * projection;
	scale = glm::scale(
		glm::mat4(1.0f),
		glm::vec3(m_screen_width / 2.0, m_screen_height / 2.0, 1.0f)
	);
	m_screen_to_pixel_space_projection = scale * projection;
}