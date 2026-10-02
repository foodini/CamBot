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
	m_telemetry_offset(pfm->get_telemetry_offset()),
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
	m_telemetry_offset = offset;
	save_project();
	return true;
}

bool EnvConfig::launch_time(float launch_time) { 
	m_launch_time = launch_time; 
	save_project();
	return true;
}

int32_t EnvConfig::telemetry_index() const {
	float telemetry_elapsed = media_in_elapsed() - m_telemetry_offset;
	return (int64_t)(telemetry_elapsed * TELEMETRY_FREQUENCY);
}

const TelemetrySlice& EnvConfig::telemetry_slice() const {
	return (*TelemetryMgr::instance)[telemetry_index()];
}

void EnvConfig::save_project() {
	project_file_mgr->set_launch_time(m_launch_time);
	project_file_mgr->set_telemetry_offset(m_telemetry_offset);
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