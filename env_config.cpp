#include "env_config.h"
#include "telemetry.h"
#include "media_container_manager.h"

EnvConfig* EnvConfig::c_instance = nullptr;

EnvConfig::EnvConfig(MediaContainerMgr& mcm, TelemetryMgr& tm, FontManager& fm, float screen_width, float screen_height, float ui_height) :
	m_media_mgr(mcm),
	m_telemetry_mgr(tm),
	m_font_manager(fm),
	m_launch_time(0.0),
	m_telemetry_offset(0.0),
	m_screen_width(screen_width),
	m_screen_height(screen_height),
	m_ui_height(ui_height)
{
	if (c_instance != nullptr) {
		throw "Cannot create more than one EnvConfig";
	}
	c_instance = this;
}

float EnvConfig::time_parametric() const {
	return m_media_mgr.in_parametric();
}

float EnvConfig::media_in_elapsed() const {
	return m_media_mgr.in_elapsed();
}

float EnvConfig::media_in_duration() const {
	return m_media_mgr.in_duration();
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
	return true;
}

int32_t EnvConfig::telemetry_index() const {
	float telemetry_elapsed = media_in_elapsed() - m_telemetry_offset;
	return (int64_t)(telemetry_elapsed * TELEMETRY_FREQUENCY);
}

const TelemetrySlice& EnvConfig::telemetry_slice() const {
	return m_telemetry_mgr[telemetry_index()];
}