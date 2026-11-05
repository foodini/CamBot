#include <algorithm>

#include "env_config.h"
#include "media_container_manager.h"
#include "telemetry.h"
#include "util.h"

EnvConfig* EnvConfig::instance = nullptr;

EnvConfig::EnvConfig(MediaContainerMgr* mcm, FontManager* fm, ProjectFileManager* pfm, EditModel* model, float screen_width, float screen_height) :
	media_mgr(mcm),
	font_mgr(fm),
	project_file_mgr(pfm),
	edit_model(model),
	m_altitude_refs(pfm->get_altitude_references()),
	m_user_settings(),
	m_screen_width(screen_width),
	m_screen_height(screen_height),
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
	return true;
}

int32_t EnvConfig::telemetry_index() const {
	return TelemetryMgr::instance->index_at(flight_elapsed());
}

const TelemetrySlice& EnvConfig::telemetry_slice() const {
	return TelemetryMgr::instance->slice_at(flight_elapsed());
}

// The window of telemetry that is shown: what was recorded between the first frame of footage used
// and the last (see EditModel::used_telemetry_span). It follows the clips and the sync as they change,
// so trimming the footage trims the map and the graph with it; telemetry from before the first
// clip, say while the pilot was getting ready, or after the last, is left out. With no clips (which
// doesn't happen) it is empty at the start of the recording.
float EnvConfig::telemetry_window_start_elapsed() const {
	double start = 0.0, end = 0.0;
	if (!edit_model->used_telemetry_span(&start, &end))
		return 0.0f;
	return (float)start;
}

float EnvConfig::telemetry_window_end_elapsed() const {
	double start = 0.0, end = 0.0;
	if (!edit_model->used_telemetry_span(&start, &end))
		return 0.0f;
	return (float)end;
}

// A telemetry sample index for a time in telemetry seconds, held inside the recording.
static int32_t clamped_telemetry_index(float telemetry_elapsed) {
	int32_t index = TelemetryMgr::instance->index_for_elapsed(telemetry_elapsed);
	int32_t last_valid = (int32_t)TelemetryMgr::instance->size() - 1;
	if (index < 0) {
		return 0;
	} else if (index > last_valid) {
		return last_valid;
	}
	return index;
}

// Where the launch marker falls in telemetry (it moves with the sync, as it is a moment in the footage).
int32_t EnvConfig::telemetry_launch_index() const {
	return clamped_telemetry_index((float)edit_model->telemetry_seconds(edit_model->launch_flight_seconds()));
}

int32_t EnvConfig::telemetry_window_start_index() const {
	return clamped_telemetry_index(telemetry_window_start_elapsed());
}

int32_t EnvConfig::telemetry_window_end_index() const {
	return clamped_telemetry_index(telemetry_window_end_elapsed());
}

// The altitude references are for the start and end of the video, which is the stretch of telemetry shown.
int32_t EnvConfig::telemetry_video_start_index() const {
	return telemetry_window_start_index();
}

int32_t EnvConfig::telemetry_video_end_index() const {
	return telemetry_window_end_index();
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
// the media manager owns the rotation, and so on), and the keys and the sync view change them
// directly. So the copy is refreshed from them whenever it's needed.
void EnvConfig::push_state_to_project() {
	ProjectData* data = project_file_mgr->mutable_data();
	AVRational tick = media_mgr->tick_time_base();
	project_io::fill_from_model(*edit_model, tick.num, tick.den, data);
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