#include <algorithm>

#include "env_config.h"
#include "media_container_manager.h"
#include "telemetry.h"
#include "util.h"

EnvConfig* EnvConfig::instance = nullptr;

EnvConfig::EnvConfig(MediaContainerMgr* mcm, FontManager* fm, ProjectFileManager* pfm, EditModel* model, float screen_width, float screen_height, float ui_height) :
	media_mgr(mcm),
	font_mgr(fm),
	project_file_mgr(pfm),
	edit_model(model),
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

float EnvConfig::flight_elapsed() const {
	return (float)edit_model->flight_seconds_for_timeline(media_mgr->current_ticks());
}

float EnvConfig::flight_duration() const {
	return (float)edit_model->ticks_to_seconds(edit_model->capture().total_ticks());
}

float EnvConfig::flight_time() const {
	return flight_elapsed() - (float)edit_model->launch_flight_seconds();
}

bool EnvConfig::telemetry_offset(float offset) {
	TelemetryMgr::instance->set_offset(offset);
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
	return TelemetryMgr::instance->elapsed_at(flight_elapsed());
}

bool EnvConfig::launch_time(float launch_time) {
	edit_model->set_launch_flight_seconds((double)launch_time);
	// Snapshot telemetry's elapsed time at this launch marker, using whatever offset is in
	// effect right now -- see TelemetryMgr::window_start_elapsed()'s doc comment for why this
	// is a one-time snapshot rather than a live formula.
	TelemetryMgr::instance->set_window_start_elapsed(TelemetryMgr::instance->elapsed_at(launch_time));
	return true;
}

int32_t EnvConfig::telemetry_index() const {
	return TelemetryMgr::instance->index_at(flight_elapsed());
}

const TelemetrySlice& EnvConfig::telemetry_slice() const {
	return TelemetryMgr::instance->slice_at(flight_elapsed());
}

// TelemetryMgr::window_start_elapsed() is where the launch marker falls in telemetry. The window
// itself starts as far before that as the marker is into the video -- i.e. at the video's own
// first frame -- so it spans exactly what is rendered, whether or not there's footage before
// launch. (It used to start at the launch marker and run one video-duration from there, which
// overshot the end of the video by however much footage preceded launch.)
float EnvConfig::telemetry_window_start_elapsed() const {
	return TelemetryMgr::instance->window_start_elapsed() - (float)edit_model->launch_flight_seconds();
}

int32_t EnvConfig::telemetry_launch_index() const {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(TelemetryMgr::instance->window_start_elapsed());
	int32_t last_valid = (int32_t)TelemetryMgr::instance->size() - 1;
	if (index < 0) {
		return 0;
	} else if (index > last_valid) {
		return last_valid;
	}
	return index;
}

int32_t EnvConfig::telemetry_window_start_index() const {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(telemetry_window_start_elapsed());
	int32_t last_valid = (int32_t)TelemetryMgr::instance->size() - 1;
	if (index < 0) {
		return 0;
	} else if (index > last_valid) {
		return last_valid;
	}
	return index;
}

int32_t EnvConfig::telemetry_window_end_index() const {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(telemetry_window_start_elapsed() + flight_duration());
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
	int32_t index = telemetry->index_for_elapsed(telemetry->elapsed_at(flight_duration()));
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
	// From the launch marker, not the start of the window: footage of the walk up to launch
	// isn't distance flown. Zero until launch.
	const TelemetrySlice& launch_slice = (*TelemetryMgr::instance)[telemetry_launch_index()];
	return (std::max)(0.0f, telemetry_slice().m_total_distance - launch_slice.m_total_distance);
}

// The project file's idea of the state is only a copy: the live values belong to their owners
// (the edit model owns the clips, the sync and the launch mark, telemetry owns the window start,
// the media manager owns the rotation, and so on), and the keys and scrub bars change them
// directly. So the copy is refreshed from them whenever it's needed.
void EnvConfig::push_state_to_project() {
	ProjectData* data = project_file_mgr->mutable_data();
	AVRational tick = media_mgr->tick_time_base();
	project_io::fill_from_model(*edit_model, tick.num, tick.den, data);
	data->window_start_elapsed_s = TelemetryMgr::instance->window_start_elapsed();
	data->rotation_rad = media_mgr->rotation_angle();
	data->altitude_refs = m_altitude_refs;
}

void EnvConfig::poll_project() {
	push_state_to_project();
	project_file_mgr->autosave_if_due(ffsw::elapsed());
}

bool EnvConfig::save_project() {
	push_state_to_project();
	return project_file_mgr->save_project();
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