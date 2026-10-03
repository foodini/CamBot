#include "stdio.h"

#include <cmath>

#include "interaction_manager.h"
#include "telemetry.h"
#include "util.h"
#include "widget.h"

DateTimeWidget::DateTimeWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "date_time_widget.vert", "date_time_widget.frag")
{

}

DateTimeWidget::~DateTimeWidget() {

}

void DateTimeWidget::render() {
	const EnvConfig* env_config = EnvConfig::instance;

	WidgetBase::render_mask();
	
	//We only need the background mask for this widget.
	//m_shader.use();
	//WidgetBase::render(m_shader);

	char buf[50];
	const TelemetrySlice& ts = env_config->telemetry_slice();
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("Date:   %04d.%02d.%02d", ts.year(), ts.month(), ts.day()),
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.8), glm::vec3(1.0, 1.0, 1.0), 0.5, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("Time:     %02d:%02d:%02d", ts.hour(), ts.minute(), ts.second()),
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.5), glm::vec3(1.0, 1.0, 1.0), 0.5, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));

	float total_sec = env_config->flight_time();
	strcpy(buf, total_sec >= 0.0 ? "Duration +" : "Duration -");
	ffsw::make_time(&buf[strlen(buf)], total_sec, false);
	env_config->font_mgr->add_string(
		StringAndProperties(
			buf, 
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.2), glm::vec3(1.0, 1.0, 1.0), 0.5, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));
}


// --- Scan-rate tuning for the right-click "scan" gesture below. SCAN_DEADZONE/SCAN_MAX_DRAG are
//     in the same NDC units as InteractionMgr::mouse_x_pos() (full screen width spans -1..1). ---
static const float SCAN_DEADZONE = 0.01f;              // No motion below this -- avoids drift from a near-stationary click.
static const float SCAN_MAX_DRAG = 0.5f;               // Offset at which the rate saturates at the max speed.
static const float SCAN_MIN_RATE_FPS = 5.0f;           // Rate right at the edge of the deadzone.
static const float SCAN_MAX_SPEED_MULTIPLIER = 50.0f;  // Rate at SCAN_MAX_DRAG, as a multiple of native_rate().
static const int64_t SCAN_FAST_THRESHOLD_FRAMES = 8;   // Skipping more than this many units in one tick
                                                       // switches from a precise per-unit step to the
                                                       // step_fast() path -- see ScrubWidget::handle_input()
                                                       // below (currently unreachable; kept as a safety net).

ScrubWidget::ScrubWidget(float width, float height, float x_pos, float y_pos,
		const std::string& vert, const std::string& frag) :
	WidgetBase(width, height, x_pos, y_pos, vert, frag)
{

}

ScrubWidget::~ScrubWidget() {

}

MediaScrubWidget::MediaScrubWidget(float width, float height, float x_pos, float y_pos) :
	ScrubWidget(width, height, x_pos, y_pos, "media_scrub_widget.vert", "media_scrub_widget.frag")
{

}

MediaScrubWidget::~MediaScrubWidget() {

}

float ScrubWidget::scan_rate_for_offset(float dx) const {
	float magnitude = fabsf(dx);
	if (magnitude < SCAN_DEADZONE) {
		return 0.0f;
	}

	float max_rate_fps = SCAN_MAX_SPEED_MULTIPLIER * native_rate();
	float t = (magnitude - SCAN_DEADZONE) / (SCAN_MAX_DRAG - SCAN_DEADZONE);
	if (t > 1.0f) {
		t = 1.0f;
	}
	// Exponential ramp: fine control just past the deadzone, but still able to reach a very fast
	// shuttle (potentially 1000+ units/sec) within a comfortable drag distance.
	float rate_fps = SCAN_MIN_RATE_FPS * powf(max_rate_fps / SCAN_MIN_RATE_FPS, t);
	return (dx < 0.0f) ? -rate_fps : rate_fps;
}

void ScrubWidget::handle_input() {
	InteractionMgr* interaction_mgr = InteractionMgr::instance();
	float mouse_x = interaction_mgr->mouse_x_pos();
	float mouse_y = interaction_mgr->mouse_y_pos();
	bool over_widget = mouse_x >= m_x_pos && mouse_x < m_x_pos + m_width &&
		mouse_y >= m_y_pos && mouse_y < m_y_pos + m_height;

	// --- Left button: click-and-hold to scrub to an absolute position. ---
	if (interaction_mgr->mouse_button_down() && over_widget) {
		m_dragging = true;
		EnvConfig::instance->pause();
	}
	if (m_dragging) {
		if (interaction_mgr->mouse_button_up()) {
			m_dragging = false;
		} else {
			// Clamp so dragging past either edge of the bar still lands exactly at the start/end,
			// and so moving the mouse above/below the bar while still held keeps scrubbing instead
			// of stopping -- only the x position matters once a drag has started.
			float clamped_x = mouse_x;
			if (clamped_x < m_x_pos) {
				clamped_x = m_x_pos;
			} else if (clamped_x > m_x_pos + m_width) {
				clamped_x = m_x_pos + m_width;
			}
			float parametric = (clamped_x - m_x_pos) / m_width;
			seek_to_parametric(parametric);
		}
	}

	// --- Right button: click-and-drag to scan forward/backward at a variable rate. ---
	if (interaction_mgr->mouse_right_button_down() && over_widget) {
		m_scanning = true;
		m_scan_origin_x = mouse_x;
		m_scan_frame_accumulator = 0.0f;
		m_last_scan_tick_time = ffsw::elapsed();
		EnvConfig::instance->pause();
	}
	if (m_scanning) {
		if (interaction_mgr->mouse_right_button_up()) {
			m_scanning = false;
		} else {
			float now = ffsw::elapsed();
			float dt = now - m_last_scan_tick_time;
			m_last_scan_tick_time = now;

			float rate_fps = scan_rate_for_offset(mouse_x - m_scan_origin_x);
			m_scan_frame_accumulator += rate_fps * dt;

			// advance_to()'s seek-then-decode loop always decodes at least one frame, even when
			// asked to move by zero -- so only call it once there's at least one whole frame to move.
			int64_t whole_frames = (int64_t)m_scan_frame_accumulator;
			if (whole_frames != 0) {
				// Capped to one unit per tick, in both directions, on purpose -- this is
				// meant for quick local searches (small, cache-friendly steps through step()),
				// which never need more than real one-unit-at-a-time speed. Anything that
				// wants to skip farther is step_fast()'s job now, so the
				// fast branch below is no longer reachable from here, but stays in
				// place as a safety net.
				whole_frames = (whole_frames > 0) ? 1 : -1;
				m_scan_frame_accumulator -= (float)whole_frames;
				int64_t abs_whole_frames = (whole_frames < 0) ? -whole_frames : whole_frames;
				if (abs_whole_frames <= SCAN_FAST_THRESHOLD_FRAMES) {
					// Small enough to step precisely, same as always.
					step(whole_frames);
				} else {
					// Fast scanning: see step_fast()'s own documentation.
					step_fast(whole_frames);
				}
			}
		}
	}
}

float MediaScrubWidget::current_parametric() const {
	return EnvConfig::instance->time_parametric();
}

float MediaScrubWidget::native_rate() const {
	return EnvConfig::instance->frame_rate();
}

void MediaScrubWidget::seek_to_parametric(float parametric) {
	// Fast/approximate on purpose: an exact-frame advance_to_parametric() here can mean
	// decoding a whole GOP (seconds, at this footage's keyframe spacing) per mouse-move tick.
	// This snaps to the nearest earlier keyframe instead, which is instant regardless of GOP
	// length or resolution; use the single-frame step keys afterward to land on an exact frame.
	EnvConfig::instance->seek_to_parametric_fast(parametric);
}

void MediaScrubWidget::step(int64_t delta) {
	if (delta > 0) {
		EnvConfig::instance->advance_by((uint64_t)delta);
	} else {
		EnvConfig::instance->rewind_by((uint64_t)(-delta));
	}
}

void MediaScrubWidget::step_fast(int64_t delta) {
	// Fast scanning: decoding every skipped frame one at a time would cost the same as
	// real-time playback no matter how fast the requested rate is. Snap to the nearest
	// keyframe toward the target instead -- same reasoning as the left-button drag above.
	EnvConfig::instance->seek_by_frames_fast(delta);
}

void MediaScrubWidget::render() {
	const EnvConfig* env_config = EnvConfig::instance;

	WidgetBase::render_mask();

	// Uniform settings must happen after a use() call
	m_shader.use();
	m_shader.setFloat("time_parametric", env_config->time_parametric());
	WidgetBase::render(m_shader);

	char buf[50];
	ffsw::make_time(buf, env_config->media_in_elapsed(), true);
	strcpy(&buf[strlen(buf)], " / ");
	ffsw::make_time(&buf[strlen(buf)], env_config->media_in_duration(), true);
	env_config->font_mgr->add_string(
		StringAndProperties(
			std::string(buf),
			0, glm::vec2(m_x_pos + 0.005, m_y_pos + m_height * 0.5), glm::vec3(1.0, 1.0, 1.0), 0.33f, 1.0f,
			StringAndProperties::V_ALIGN::V_CENTER));
}

TelemetryScrubWidget::TelemetryScrubWidget(float width, float height, float x_pos, float y_pos) :
	ScrubWidget(width, height, x_pos, y_pos, "media_scrub_widget.vert", "telemetry_scrub_widget.frag")
{

}

TelemetryScrubWidget::~TelemetryScrubWidget() {

}

float TelemetryScrubWidget::current_parametric() const {
	return TelemetryMgr::instance->parametric_at(EnvConfig::instance->media_in_elapsed());
}

float TelemetryScrubWidget::native_rate() const {
	return TELEMETRY_FREQUENCY;
}

float TelemetryScrubWidget::offset_for_parametric_at(float parametric, float reference_media_elapsed) {
	// There's no decode cost for telemetry -- adjusting the offset is just a float assignment --
	// so, unlike MediaScrubWidget, this is an exact solve rather than a fast/approximate snap.
	float desired_elapsed = parametric * TelemetryMgr::instance->duration();
	return reference_media_elapsed - desired_elapsed;
}

void TelemetryScrubWidget::seek_to_parametric(float parametric) {
	EnvConfig::instance->telemetry_offset(
		offset_for_parametric_at(parametric, EnvConfig::instance->media_in_elapsed()));
}

void TelemetryScrubWidget::step(int64_t delta) {
	EnvConfig::instance->telemetry_offset(EnvConfig::instance->telemetry_offset() - delta / (float)TELEMETRY_FREQUENCY);
}

void TelemetryScrubWidget::step_fast(int64_t delta) {
	// No decode cost either way -- same path as step().
	step(delta);
}

// Clamped [0,1] position of a cutoff marker, read from EnvConfig's launch-marker-derived
// telemetry_window_start_elapsed() -- not interactive, drawn purely for feedback (see the
// doc comment on the declaration in widget.h). *in_bounds reports whether the true,
// unclamped position actually fell inside [0,1] -- false means we've run out of recorded
// telemetry on that side.
float TelemetryScrubWidget::cutoff_parametric(bool is_left, bool* in_bounds) const {
	const EnvConfig* env_config = EnvConfig::instance;
	float window_start = env_config->telemetry_window_start_elapsed();
	float target_elapsed = is_left ? window_start : window_start + env_config->media_in_duration();
	float raw = target_elapsed / TelemetryMgr::instance->duration();
	*in_bounds = (raw >= 0.0f) && (raw <= 1.0f);
	if (raw < 0.0f) {
		raw = 0.0f;
	} else if (raw > 1.0f) {
		raw = 1.0f;
	}
	return raw;
}

// Purely cosmetic now -- sizes render()'s soft halo around each cutoff marker. The markers
// aren't clickable, so this is no longer also a hit-test radius.
static const float CUTOFF_HALO_RADIUS_PX = 7.0f;

void TelemetryScrubWidget::render() {
	const EnvConfig* env_config = EnvConfig::instance;

	WidgetBase::render_mask();

	bool left_in_bounds, right_in_bounds;
	float left_parametric = cutoff_parametric(true, &left_in_bounds);
	float right_parametric = cutoff_parametric(false, &right_in_bounds);

	// White when the cutoff sits inside the actual recorded telemetry, mid-grey when it's been
	// clamped to the edge of the bar because we ran out of telemetry on that side. Red/green are
	// deliberately avoided here.
	float left_shade = left_in_bounds ? 1.0f : 0.5f;
	float right_shade = right_in_bounds ? 1.0f : 0.5f;

	float marker_halfwidth = 1.0f * 2.0f / (m_width * env_config->screen_width());
	float halo_halfwidth = CUTOFF_HALO_RADIUS_PX * 2.0f / (m_width * env_config->screen_width());

	// Uniform settings must happen after a use() call
	m_shader.use();
	m_shader.setFloat("time_parametric", current_parametric());
	m_shader.setFloat("cutoff_left", left_parametric);
	m_shader.setFloat("cutoff_right", right_parametric);
	m_shader.setFloat3("cutoff_left_color", left_shade, left_shade, left_shade);
	m_shader.setFloat3("cutoff_right_color", right_shade, right_shade, right_shade);
	m_shader.setFloat("marker_halfwidth", marker_halfwidth);
	m_shader.setFloat("halo_halfwidth", halo_halfwidth);
	WidgetBase::render(m_shader);
}

MapWidget::MapWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "map_widget.vert", "map_widget.frag"),
	m_shader_course("map_widget_1_course.vert", "map_widget_1_course.frag"),
	m_shader_arrow("map_widget_2_arrow.vert", "map_widget_2_arrow.frag"),
	m_center_lat(nanf("")),
	m_center_lon(nanf("")),
	m_course_lines(8.0, 0.5),
	m_vertex_attrib_sizes{2, 1}
{
	glGenVertexArrays(1, &m_course_vao);
	glGenBuffers(1, &m_course_vbo);
}

MapWidget::~MapWidget() {
}

glm::vec2 MapWidget::latlon_to_coords(float lat, float lon) {
	double dist_x = ((double)lon - m_center_lon) / 90.0 * 10000000.0;
	dist_x *= cos(M_PI / 2.0 * lat / 90.0);
	double dist_y = ((double)lat - m_center_lat) / 90.0 * 10000000.0;

	return glm::vec2(dist_x, dist_y);
}

void MapWidget::polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices) {
	TelemetryMgr& telemetry_mgr = *TelemetryMgr::instance;

	if (index == 0) {
		m_center_lat = slice.m_gps_lat;
		m_center_lon = slice.m_gps_lon;
	}

	glm::vec2 xy = latlon_to_coords(telemetry_mgr[index].m_gps_lat, telemetry_mgr[index].m_gps_lon);

	std::vector<float> supp;
	supp.push_back(slice.m_climb_rate[1]);

	m_course_lines.add_segment(xy, supp);
}


void MapWidget::_set_uniforms(Shader& shader) {
	const EnvConfig* env_config = EnvConfig::instance;

	shader.use();
	shader.setFloat("xpos", m_x_pos);
	shader.setFloat("ypos", m_y_pos);
	shader.setFloat("width", m_width);
	shader.setFloat("height", m_height);
	shader.setFloat("time", env_config->media_in_elapsed());
	shader.setFloat("time_parametric", env_config->time_parametric());
	shader.setFloat("course", env_config->telemetry_slice().course_rad());
	shader.setFloat("speed", env_config->telemetry_slice().speed_mph());
}

void MapWidget::set_uniforms() {
	_set_uniforms(m_shader);
	_set_uniforms(m_shader_course);
	_set_uniforms(m_shader_arrow);
}

//TODO(P1): make this a member of MapWidget
void set_map_widget_vertex_attrib_pointers() {
	// The first value is the ID of the attribute in the shader layout
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(0 * sizeof(float)));  //xy
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(2 * sizeof(float)));  // 1-float color
	glEnableVertexAttribArray(1);
}


void MapWidget::render_course() {
	const EnvConfig* env_config = EnvConfig::instance;

	// TODO(P0): THIS MAY NOT BE THE CORRECT PROJECTION. The "square" on the screen is not square in canonical screen space.
	//           It's dependent upon the aspect ratio of the entire window. I suspect that the projection is correct, but it
	//           needs to be checked.
	glm::mat4 projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f);
	glm::vec4 min_clip(m_x_pos, m_y_pos, 0.0f, 1.0f);
	glm::vec4 max_clip(m_x_pos + m_width, m_y_pos + m_height, 0.0f, 1.0f);
	min_clip = projection * min_clip;
	max_clip = projection * max_clip;

	projection = glm::mat4(1.0f);
	glm::mat4 identity(1.0f);

	// Set current pilot position at the center of the space
	const TelemetrySlice& ts = env_config->telemetry_slice();
	glm::vec2 position = latlon_to_coords(ts.m_gps_lat, ts.m_gps_lon);
	projection = glm::translate(identity, glm::vec3(-position, 0.0)) * projection;

	// Scale the data to have the displayed data in the [-1.0..1.0, -1.0..1.0] ranges
	static float last_speed_scale = 1.0; //TODO(P0) move this to be a member.
	float speed_scale = glm::min(1.0f, 1.0f / env_config->telemetry_slice().speed_mph() + 0.3f);
	last_speed_scale = speed_scale * 0.01f + last_speed_scale * 0.99f;
	float widget_dimension_scale = max_clip.x - min_clip.x;
	glm::vec3 scale_vec(0.01f * last_speed_scale * widget_dimension_scale);  // 0.01 to make the widget 200m tall when pilot stationary.
	projection = glm::scale(identity, scale_vec) * projection;
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("scale: %f", last_speed_scale),
			0, glm::vec2(m_x_pos, m_y_pos + m_height), glm::vec3(1, 1, 1), 0.5, 2.0));

	// Recenter the draw over the center of the widget
	glm::mat4 xlate = glm::translate(identity, glm::vec3((max_clip.x + min_clip.x) / 2.0f, (max_clip.y + min_clip.y) / 2.0f, 0.0f));
	projection = xlate * projection;

	// YOU MUST use() the shader before setting its uniforms. I'm wondering if use() clears out any existing uniforms.
	m_shader_course.use();
	m_shader_course.setFloat2("min_clip", min_clip.x, min_clip.y);
	m_shader_course.setFloat2("max_clip", max_clip.x, max_clip.y);

	glUniformMatrix4fv(glGetUniformLocation(m_shader_course.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	m_course_lines.render(set_map_widget_vertex_attrib_pointers);
}

// Rebuilds m_course_lines (and recenters the local lat/lon projection on the window's own
// start) straight from TelemetryMgr, scoped to [start_index, end_index] -- i.e. exactly what
// polygonalize() already does per-sample, just driven directly instead of through the
// incremental parse callback. polygonalize() itself is untouched, so the map still
// progressively fills in while the file is still parsing; this takes over as soon as parsing
// completes, and again every time the window changes (the TSW's offset or cutoffs move).
void MapWidget::rebuild_course_for_window(int32_t start_index, int32_t end_index) {
	TelemetryMgr& telemetry_mgr = *TelemetryMgr::instance;

	const TelemetrySlice& window_start_slice = telemetry_mgr[start_index];
	m_center_lat = window_start_slice.m_gps_lat;
	m_center_lon = window_start_slice.m_gps_lon;

	m_course_lines.clear();
	for (int32_t i = start_index; i <= end_index; i++) {
		const TelemetrySlice& slice = telemetry_mgr[i];
		glm::vec2 xy = latlon_to_coords(slice.m_gps_lat, slice.m_gps_lon);

		std::vector<float> supp;
		supp.push_back(slice.m_climb_rate[1]);

		m_course_lines.add_segment(xy, supp);
	}
}

void MapWidget::render() {
	if (TelemetryMgr::instance->parsing_done()) {
		const EnvConfig* env_config = EnvConfig::instance;
		int32_t start_index = env_config->telemetry_window_start_index();
		int32_t end_index = env_config->telemetry_window_end_index();
		if (start_index != m_window_start_index || end_index != m_window_end_index) {
			rebuild_course_for_window(start_index, end_index);
			m_window_start_index = start_index;
			m_window_end_index = end_index;
		}
	}

	WidgetBase::render_mask();
	
	set_uniforms();
	
	//m_shader.use();
	//WidgetBase::render(m_shader);
	
	render_course();

	m_shader_arrow.use();
	WidgetBase::render(m_shader_arrow);

	const EnvConfig* env_config = EnvConfig::instance;

	const TelemetrySlice& ts = env_config->telemetry_slice();
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("%5.1fmph", ts.speed_mph()),
			0, glm::vec2(m_x_pos, m_y_pos), glm::vec3(1.0, 1.0, 1.0), 0.5, 2.5));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("%3.0f ", ts.m_course_deg),
			0, glm::vec2(m_x_pos + m_width, m_y_pos), glm::vec3(1.0, 1.0, 1.0), 0.5, 2.5,
			StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_RIGHT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			"o",
			0, glm::vec2(m_x_pos + m_width, m_y_pos + 0.01), glm::vec3(1.0, 1.0, 1.0), 0.33f, 2.5,
			StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_RIGHT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("%+6.1ffpm", ts.m_climb_rate[1]),
			0, glm::vec2(m_x_pos, m_y_pos + m_height), glm::vec3(1, 1, 1), 0.5, 2.5,
			StringAndProperties::V_ALIGN::V_TOP, StringAndProperties::H_ALIGN::H_LEFT));
}

GraphWidget::GraphWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "graph_widget_0_background.vert", "graph_widget_0_background.frag"),
	m_alt_body_shader("graph_widget_1_alt_body.vert", "graph_widget_1_alt_body.frag"),
	m_alt_outline_shader("graph_widget_2_alt_outline.vert", "graph_widget_2_alt_outline.frag"),
	m_alt_position_shader("graph_widget_3_alt_position.vert", "graph_widget_3_alt_position.frag"),
	m_next_index(0.0),
	m_alt_min(1000000.0),
	m_alt_max(-1000000.0),
	m_below_min_alt(0.0),
	m_alt_body_vect(),
	m_alt_outline_lines(3.0, 0.5)
{
	glGenVertexArrays(1, &m_alt_body_vao);
	glGenBuffers(1, &m_alt_body_vbo);
	glGenVertexArrays(1, &m_alt_outline_vao);
	glGenBuffers(1, &m_alt_outline_vbo);
	glGenVertexArrays(1, &m_alt_position_vao);
	glGenBuffers(1, &m_alt_position_vbo);
}

void GraphWidget::update_graph_to_screen_projection() {
	EnvConfig* env_config = EnvConfig::instance;
	float min_x = m_alt_body_vect[0];
	float max_x = m_alt_body_vect[m_alt_body_vect.size() - 2];

	// move the point at <0.0, m_below_min_alt> to <0.0, 0.0>:
	glm::mat4 projection = glm::mat4(1.0f);
	glm::mat4 xlate = glm::translate(
		glm::mat4(1.0f),
		glm::vec3(-min_x, -m_below_min_alt, 0.0)
	);
	projection = xlate * projection;
	glm::mat4 scale = glm::scale(
		glm::mat4(1.0f),
		glm::vec3(m_width/(1.0 + max_x - min_x), m_height/(m_alt_max - m_below_min_alt), 1.0)
	);
	projection = scale * projection;
	glm::mat4 xlate2 = glm::translate(
		glm::mat4(1.0f),
		glm::vec3(m_x_pos, m_y_pos, 0.0)
	);
	
	m_graph_to_screen_projection = xlate2 * projection;
}

void GraphWidget::polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices) {
	const EnvConfig* env_config = EnvConfig::instance;

	if (index == 0) {
		if (m_alt_body_vect.size() > 0) {
			m_alt_body_vect.clear();
		}
		if (m_alt_outline_lines.size() > 0) {
			m_alt_outline_lines.clear();
		}
		m_alt_max = slice.m_alt[1];
		m_alt_min = slice.m_alt[1];

		m_next_index = 0.0f;
	}

	float stride = num_slices / (env_config->screen_width() * m_width * 2.0f); // Doubling in case we get some antialiasing help.
	if ((float)index >= m_next_index) {
		// TODO(P1): This is O(n^2) with an average runtime around O(n). Maybe only do the adjustment every n seconds?
		if (slice.m_alt[1] < m_alt_min || slice.m_alt[1] > m_alt_max) {
			m_alt_max = glm::max(m_alt_max, slice.m_alt[1]);
			m_alt_min = glm::min(m_alt_min, slice.m_alt[1]);
			m_below_min_alt = m_alt_min - 0.05f * (m_alt_max - m_alt_min);
			for (int i = 3; i < m_alt_body_vect.size(); i += 4) {
				m_alt_body_vect[i] = m_below_min_alt;
			}
		}

		m_alt_body_vect.push_back(m_next_index);
		m_alt_body_vect.push_back(slice.m_alt[1]);
		m_alt_body_vect.push_back(m_next_index);
		m_alt_body_vect.push_back(m_below_min_alt);

		glm::vec2 xy(m_next_index, slice.m_alt[1]);
		std::vector<float> sup;
		m_alt_outline_lines.add_segment(xy, sup);

		update_graph_to_screen_projection();
		m_next_index += stride;
	}
}

GraphWidget::~GraphWidget() {

}

void GraphWidget::render_alt_body() {
	if (m_alt_body_vect.size() <= 2)
		return;

	EnvConfig* env_config = EnvConfig::instance;

	m_alt_body_shader.use();

	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glEnable(GL_CLIP_DISTANCE0);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindVertexArray(m_alt_body_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_alt_body_vbo);

	// The first value is the ID of the attribute in the shader layout
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(0 * sizeof(float)));  //xy
	glEnableVertexAttribArray(0);

	glm::vec4 test_point(m_next_index/2.0, (m_alt_max + m_alt_min) / 2.0f, 1.0, 1.0);
	test_point = test_point * m_graph_to_screen_projection;

	// Draw the body.
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_alt_body_vect.size(), m_alt_body_vect.data(), GL_STATIC_DRAW);
	glUniformMatrix4fv(glGetUniformLocation(m_alt_body_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(m_graph_to_screen_projection));
	glBindVertexArray(m_alt_body_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)(m_alt_body_vect.size() / 2));  // Last arg is NUMBER OF VERTS!!!!
		
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void GraphWidget::render_alt_outline() {
	m_alt_outline_shader.use();
	glUniformMatrix4fv(glGetUniformLocation(m_alt_body_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(m_graph_to_screen_projection));
	m_alt_outline_lines.render();
}

void GraphWidget::render_pilot_position() {
	if (m_alt_body_vect.size() <= 2)
		return;

	EnvConfig* env_config = EnvConfig::instance;

	m_alt_position_shader.use();

	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glEnable(GL_CLIP_DISTANCE0);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindVertexArray(m_alt_position_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_alt_position_vbo);

	// The first value is the ID of the attribute in the shader layout
	// TODO(P0): Do I really have to do this every frame? Can I set these Attribs in the constructor? Would
	//           allow me to remove the callback.
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(0 * sizeof(float)));  //xy
	glEnableVertexAttribArray(0);
	glPointSize(50.0);
	
	m_alt_position_verts[0] = (float)env_config->telemetry_index();
	m_alt_position_verts[1] = env_config->telemetry_slice().m_alt[1];

	// Draw the body.
	glBufferData(GL_ARRAY_BUFFER, sizeof(m_alt_position_verts), m_alt_position_verts, GL_STATIC_DRAW);
	glUniformMatrix4fv(glGetUniformLocation(m_alt_position_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(m_graph_to_screen_projection));
	glBindVertexArray(m_alt_position_vao);
	glDrawArrays(GL_POINTS, 0, 1);  // Last arg is NUMBER OF VERTS!!!!

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

// Rebuilds m_alt_body_vect/m_alt_outline_lines (and m_alt_min/m_alt_max) straight from
// TelemetryMgr, scoped to [start_index, end_index] -- the same stride/min-max logic
// polygonalize() already runs per-sample, just driven directly instead of through the
// incremental parse callback, and with the stride sized to the window's own sample count
// rather than the whole file's (otherwise, whenever telemetry runs much longer than the
// video, the graph would come out far chunkier than it needs to be). x-coordinates are kept
// as real telemetry indices (not reset to 0) so render_pilot_position()'s absolute
// telemetry_index() still projects correctly through the resulting m_graph_to_screen_projection.
// polygonalize() itself is untouched, so the graph still progressively fills in while the file
// is still parsing; this takes over as soon as parsing completes, and again every time the
// window changes (the TSW's offset or cutoffs move).
void GraphWidget::rebuild_windowed_data(int32_t start_index, int32_t end_index) {
	const EnvConfig* env_config = EnvConfig::instance;
	TelemetryMgr& telemetry_mgr = *TelemetryMgr::instance;

	m_alt_body_vect.clear();
	m_alt_outline_lines.clear();

	const TelemetrySlice& first_slice = telemetry_mgr[start_index];
	m_alt_max = first_slice.m_alt[1];
	m_alt_min = first_slice.m_alt[1];

	float stride = (float)(end_index - start_index + 1) / (env_config->screen_width() * m_width * 2.0f);
	m_next_index = (float)start_index;

	for (int32_t index = start_index; index <= end_index; index++) {
		const TelemetrySlice& slice = telemetry_mgr[index];
		if ((float)index < m_next_index) {
			continue;
		}

		if (slice.m_alt[1] < m_alt_min || slice.m_alt[1] > m_alt_max) {
			m_alt_max = glm::max(m_alt_max, slice.m_alt[1]);
			m_alt_min = glm::min(m_alt_min, slice.m_alt[1]);
			m_below_min_alt = m_alt_min - 0.05f * (m_alt_max - m_alt_min);
			for (int i = 3; i < m_alt_body_vect.size(); i += 4) {
				m_alt_body_vect[i] = m_below_min_alt;
			}
		}

		m_alt_body_vect.push_back(m_next_index);
		m_alt_body_vect.push_back(slice.m_alt[1]);
		m_alt_body_vect.push_back(m_next_index);
		m_alt_body_vect.push_back(m_below_min_alt);

		glm::vec2 xy(m_next_index, slice.m_alt[1]);
		std::vector<float> sup;
		m_alt_outline_lines.add_segment(xy, sup);

		m_next_index += stride;
	}

	if (m_alt_body_vect.size() > 0) {
		update_graph_to_screen_projection();
	}
}

void GraphWidget::render() {
	if (TelemetryMgr::instance->parsing_done()) {
		const EnvConfig* env_config = EnvConfig::instance;
		int32_t start_index = env_config->telemetry_window_start_index();
		int32_t end_index = env_config->telemetry_window_end_index();
		if (start_index != m_window_start_index || end_index != m_window_end_index) {
			rebuild_windowed_data(start_index, end_index);
			m_window_start_index = start_index;
			m_window_end_index = end_index;
		}
	}

	WidgetBase::render_mask();

	//m_shader.use(); 
	//WidgetBase::render(m_shader);

	render_alt_body();
	render_alt_outline();
	render_pilot_position();

	EnvConfig* env_config = EnvConfig::instance;
	const TelemetrySlice& ts = env_config->telemetry_slice();

	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("alt:%+6.0f'", ts.m_alt[1] * 3.28084),
			0, glm::vec2(m_x_pos, m_y_pos + m_height), glm::vec3(1, 1, 1), 0.5, 2.0,
			StringAndProperties::V_ALIGN::V_TOP, StringAndProperties::H_ALIGN::H_LEFT));
}

ClimbWidget::ClimbWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "climb_widget.vert", "climb_widget.frag")
{

}

ClimbWidget::~ClimbWidget() {

}

void ClimbWidget::render() {
	EnvConfig* env_config = EnvConfig::instance;

	m_shader.use();

	const TelemetrySlice& ts = env_config->telemetry_slice();

	m_shader.setFloat3("climb_rates", ts.m_climb_rate[0], ts.m_climb_rate[1], ts.m_climb_rate[2]);

	WidgetBase::render(m_shader);
}