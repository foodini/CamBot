#include "stdio.h"

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

	m_shader.use();
	WidgetBase::render(m_shader);

	char buf[50];
	const TelemetrySlice& ts = env_config->telemetry_slice();
	sprintf(buf, "Date:   %04d.%02d.%02d", ts.year(), ts.month(), ts.day());
	env_config->font_mgr->add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 39.0), glm::vec3(1.0, 1.0, 1.0), 1);
	sprintf(buf, "Time:     %02d:%02d:%02d", ts.hour(), ts.minute(), ts.second());
	env_config->font_mgr->add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 22.0), glm::vec3(1.0, 1.0, 1.0), 1);
	float total_sec = env_config->flight_time();
	strcpy(buf, total_sec >= 0.0 ? "Duration +" : "Duration -");
	ffsw::make_time(&buf[strlen(buf)], total_sec, false);
	env_config->font_mgr->add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 5.0), glm::vec3(1.0, 1.0, 1.0), 1);
}


MediaScrubWidget::MediaScrubWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "media_scrub_widget.vert", "media_scrub_widget.frag")
{

}

MediaScrubWidget::~MediaScrubWidget() {

}

void MediaScrubWidget::handle_input() {
	InteractionMgr* interaction_mgr = InteractionMgr::instance();

	if (interaction_mgr->mouse_button_down()) {
		if (interaction_mgr->mouse_x_pos() >= m_x_pos && interaction_mgr->mouse_x_pos() < m_x_pos + m_width &&
			interaction_mgr->mouse_y_pos() >= m_y_pos && interaction_mgr->mouse_y_pos() < m_y_pos + m_height) {
			float parametric = (interaction_mgr->mouse_x_pos() - m_x_pos) / (m_width - 1.0f);
			EnvConfig::instance->advance_to_parametric(parametric);
		}
	}
}

void MediaScrubWidget::render() {
	const EnvConfig* env_config = EnvConfig::instance;

	// Uniform settings must happen after a use() call
	m_shader.use();
	m_shader.setFloat("xpos", m_x_pos);
	m_shader.setFloat("ypos", m_y_pos);
	m_shader.setFloat("width", m_width);
	m_shader.setFloat("height", m_height);
	m_shader.setFloat("time", env_config->media_in_elapsed());
	m_shader.setFloat("time_parametric", env_config->time_parametric());

	WidgetBase::render(m_shader);

	char buf[50];
	ffsw::make_time(buf, env_config->media_in_elapsed(), true);
	strcpy(&buf[strlen(buf)], " / ");
	ffsw::make_time(&buf[strlen(buf)], env_config->media_in_duration(), true);
	env_config->font_mgr->add_string(buf, 0, .33f, glm::vec2(m_x_pos + 5.0, m_y_pos + 5.0), glm::vec3(1.0, 1.0, 1.0), 1.0f);
}

Space::Space(float tile_size) :
	m_poly_vects(),
	m_tile_size(tile_size),
	m_center_lat(nan("")),
	m_center_lon(nan("")),
	m_line_width(5.0),
	m_last_right(glm::vec2(1.0, 0.0))
{
}

Space::~Space() {
	// TODO(P0): destroy all the poly bufs
}

glm::vec2 Space::latlon_to_coords(float lat, float lon) {
	double dist_x = ((double)lon - m_center_lon) / 90.0 * 10000000.0;
	dist_x *= cos(M_PI / 2.0 * lat / 90.0);
	double dist_y = ((double)lat - m_center_lat) / 90.0 * 10000000.0;

	return glm::vec2(dist_x, dist_y);
}

Space::SpatialIndex Space::coords_to_gridref(const glm::vec2& xy_coords) {
	SpatialIndex retval;
	retval.first = xy_coords.x / m_tile_size;
	if (xy_coords.x < 0.0)
		retval.first -= 1;
	retval.second = xy_coords.y / m_tile_size;
	if (xy_coords.y < 0.0)
		retval.second -= 1;

	return retval;
}

Space::SpatialIndex Space::latlon_to_gridref(float lat, float lon) {

	return coords_to_gridref(latlon_to_coords(lat, lon));
}

std::pair<float*, uint32_t> Space::get_course_buffer(Space::SpatialIndex spatial_index) {
	//TODO(P2) Better way to get the length that allows delete of poly_vects?
	std::pair<float*, uint32_t> retval;
	auto vects = m_poly_vects.find(spatial_index);
	if (vects == m_poly_vects.end()) {
		retval.first = nullptr;
		retval.second = 0;
	}
	else {
		retval.first = m_poly_vects.find(spatial_index)->second->data();
		retval.second = m_poly_vects.find(spatial_index)->second->size();
	}
	return retval;
}

std::vector<float>* Space::get_poly_verts(const SpatialIndex& spatial_index) {
	std::vector<float>* vert_vect;
	auto poly_vect_find = m_poly_vects.find(spatial_index);
	if (poly_vect_find == m_poly_vects.end()) {
		vert_vect = new std::vector<float>;
		m_poly_vects[spatial_index] = vert_vect;
	}
	else {
		vert_vect = poly_vect_find->second;
	}
	return vert_vect;
}

void Space::push_verts(std::vector<float>* vert_vect, const glm::vec2& p1, const glm::vec2& right, float climb_rate, bool degenerate) {
	glm::vec2 vert0 = p1 - right;
	glm::vec2 vert1 = p1 + right;
	if (degenerate) {
		for (uint32_t i = 0; i < 4; i++) {
			// Push the last 4 values onto the vector again.
			vert_vect->push_back((*vert_vect)[vert_vect->size() - 4]);
		}
		vert_vect->push_back(vert0.x);
		vert_vect->push_back(vert0.y);
		vert_vect->push_back(0.0);
		vert_vect->push_back(climb_rate);
	}
	vert_vect->push_back(vert0.x);
	vert_vect->push_back(vert0.y);
	vert_vect->push_back(0.0);
	vert_vect->push_back(climb_rate);
	vert_vect->push_back(vert1.x);
	vert_vect->push_back(vert1.y);
	vert_vect->push_back(1.0);
	vert_vect->push_back(climb_rate);
}


void Space::polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices) {
	if (index == 0) {
		m_center_lat = slice.m_gps_lat;
		m_center_lon = slice.m_gps_lon;
	}

	if (index < 2 || num_slices < 3)
		return;

	TelemetryMgr& telemetry_mgr = *TelemetryMgr::instance;

	glm::vec2 p0 = latlon_to_coords(telemetry_mgr[index - 2].m_gps_lat, telemetry_mgr[index - 2].m_gps_lon);
	SpatialIndex s0 = coords_to_gridref(p0);
	glm::vec2 p1 = latlon_to_coords(telemetry_mgr[index - 1].m_gps_lat, telemetry_mgr[index - 1].m_gps_lon);
	SpatialIndex s1 = coords_to_gridref(p1);
	glm::vec2 p2 = latlon_to_coords(telemetry_mgr[index].m_gps_lat, telemetry_mgr[index].m_gps_lon);
	SpatialIndex s2 = coords_to_gridref(p2);

	//climb_rate, in m/s
	float climb_rate = 0.0;
	for (int i = 0; i < 3; i++) {
		climb_rate += (telemetry_mgr[index - 1].m_alt[i] - telemetry_mgr[index - 2].m_alt[i]);
	}
	//avg & freq:
	climb_rate /= (3.0 / TELEMETRY_FREQUENCY);
	//convert to ft/min:
	climb_rate *= 60.0 * 3.28084;

	//static float cr = -1000.0;
	//static float dcr = 9;
	//cr += dcr;
	//if (cr > 1000.0 || cr < -1000.0) {
	//	dcr = -dcr;
	//	cr += dcr;
	//}
	//climb_rate = cr;

	glm::vec2 d01 = p1 - p0;
	glm::vec2 d12 = p2 - p1;
	glm::vec2 avg_direction = d12 + d01;
	glm::vec2 right;
	if (glm::length(avg_direction) < 0.001) {
		right = m_last_right;
	}
	else {
		right = glm::normalize(glm::vec2(avg_direction.y, -avg_direction.x));
		m_last_right = right;
	}
	right *= m_line_width / 2.0;

	std::vector<float>* vert_vect0 = get_poly_verts(s0);
	std::vector<float>* vert_vect1 = get_poly_verts(s1);
	if (s0 != s1) {
		// The last pair of points went in a different gridref. We have to start over.
		// either this is a new gridref, in which case we just drop the points in, or it's
		// an old one, in which case, we have to generate a degenerate poly to move to the
		// new location.
		if (vert_vect0->size() > 0) {
			push_verts(vert_vect0, p1, right, climb_rate, false);
		}
		if (vert_vect1->size() == 0) {
			push_verts(vert_vect1, p1, right, climb_rate, false);
		}
		else {
			push_verts(vert_vect1, p1, right, climb_rate, true);
		}
	}
	else {
		push_verts(vert_vect1, p1, right, climb_rate, false);
	}
}

std::vector<std::pair<float*, uint32_t>> Space::get_course_buffers() {
	std::vector<std::pair<float*, uint32_t>> retval;

	const TelemetrySlice& slice = EnvConfig::instance->telemetry_slice();
	Space::SpatialIndex center_spatial_index = latlon_to_gridref(slice.m_gps_lat, slice.m_gps_lon);
	for (int dx = -1; dx <= 1; dx++) {
		for (int dy = -1; dy <= 1; dy++) {
			Space::SpatialIndex si(center_spatial_index.first + dx, center_spatial_index.second + dy);
			std::pair<float*, uint32_t> verts = get_course_buffer(si);
			
			if (verts.second > 2) {
				retval.push_back(verts);
			}
		}
	}
	return retval;
}

glm::vec2 Space::get_current_coords() {
	TelemetrySlice current_slice = (*TelemetryMgr::instance)[EnvConfig::instance->telemetry_index()];
	return latlon_to_coords(current_slice.m_gps_lat, current_slice.m_gps_lon);
}

Space::SpatialIndex Space::get_current_gridref() {
	TelemetrySlice current_slice = (*TelemetryMgr::instance)[EnvConfig::instance->telemetry_index()];
	return latlon_to_gridref(current_slice.m_gps_lat, current_slice.m_gps_lon);
}


MapWidget::MapWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "map_widget.vert", "map_widget.frag"),
	m_shader_course("map_widget_1_course.vert", "map_widget_1_course.frag"),
	m_shader_arrow("map_widget_2_arrow.vert", "map_widget_2_arrow.frag"),
	m_center_lat(nan("")),
	m_center_lon(nan("")),
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
	uint32_t climb_rate_baseline_index;
	if (index >= TELEMETRY_FREQUENCY) {
		climb_rate_baseline_index = index - TELEMETRY_FREQUENCY;
	} else {
		climb_rate_baseline_index = 0;
	}
	float climb_rate = telemetry_mgr[index].m_alt[1] - telemetry_mgr[climb_rate_baseline_index].m_alt[1];
	//climb_rate is currently m/s. Needs to be ft/min. (Yeah, I know.)
	climb_rate *= (60.0 * 3.28084);
	std::vector<float> supp;
	supp.push_back(climb_rate);

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
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);  //xy
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)(2 * sizeof(float)));  // 1-float color
	glEnableVertexAttribArray(1);
}


void MapWidget::render_course() {
	const EnvConfig* env_config = EnvConfig::instance;

	glm::mat4 projection = glm::ortho(0.0f, env_config->screen_width(), 0.0f, env_config->screen_height());
	glm::vec4 min_clip(m_x_pos, m_y_pos, 0.0f, 1.0f);
	glm::vec4 max_clip(m_x_pos + m_width, m_y_pos + m_height, 0.0f, 1.0f);
	min_clip = projection * min_clip;
	max_clip = projection * max_clip;
	
	// YOU MUST use() the shader before setting its uniforms. I'm wondering if use() clears out any existing uniforms.
	m_shader_course.use();
	m_shader_course.setFloat2("min_clip", min_clip.x, min_clip.y);
	m_shader_course.setFloat2("max_clip", max_clip.x, max_clip.y);

	// Center the track over the center of the widget:
	projection = glm::translate(projection, glm::vec3(m_x_pos + m_width / 2.0, m_y_pos + m_height / 2.0, 0.0));
	// Position it so the current pilot position is at the center of the widget
	const TelemetrySlice& ts = env_config->telemetry_slice();
	glm::vec2 position = latlon_to_coords(ts.m_gps_lat, ts.m_gps_lon);
	projection = glm::translate(projection, glm::vec3(-position, 0.0));

	glUniformMatrix4fv(glGetUniformLocation(m_shader_course.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	m_course_lines.render(set_map_widget_vertex_attrib_pointers);
}

void MapWidget::render() {
	set_uniforms();
	
	m_shader.use();
	WidgetBase::render(m_shader);
	
	render_course();

	m_shader_arrow.use();
	WidgetBase::render(m_shader_arrow);

	const EnvConfig* env_config = EnvConfig::instance;

	env_config->font_mgr->format(0, 0.33f, glm::vec2(m_x_pos + 3.0, m_y_pos + 3.0), glm::vec3(1.0, 1.0, 1.0), 1.5, "%4.1fmph", env_config->telemetry_slice().speed_mph());
	// TODO(P2): right-justified fonts.
	env_config->font_mgr->format(0, 0.33f, glm::vec2(m_x_pos + m_width - 40.0f, m_y_pos + 3.0f), glm::vec3(1.0, 1.0, 1.0), 1.5, "%3.0f", env_config->telemetry_slice().m_course_deg);
	env_config->font_mgr->format(0, 0.25f, glm::vec2(m_x_pos + m_width - 10.0f, m_y_pos + 8.0f), glm::vec3(1.0, 1.0, 1.0), 1.5, "o");
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
	glm::mat4 projection = glm::mat4(1.0f);
	projection = glm::translate(projection, glm::vec3(-min_x, -m_below_min_alt, 0.0));
	float relative_height = m_height / env_config->screen_height();
	float relative_width = m_width / env_config->screen_width();
	glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(
		2.0f * relative_width / (max_x - min_x),
		2.0f * relative_height / (m_alt_max - m_below_min_alt),
		1.0));
	projection = scale * projection;
	float bottom_gap = 2.0f * m_y_pos / env_config->screen_height();
	float left_gap = 2.0f * m_x_pos / env_config->screen_width();
	glm::mat4 xlate = glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f + left_gap, -1.0f + bottom_gap, 0.0));
	
	m_graph_to_screen_projection = xlate * projection;
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

	float stride = num_slices / (m_width * 2.0); // Doubling in case we get some antialiasing help.
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
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);  //xy
	glEnableVertexAttribArray(0);

	// Draw the body.
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_alt_body_vect.size(), m_alt_body_vect.data(), GL_STATIC_DRAW);
	glUniformMatrix4fv(glGetUniformLocation(m_alt_body_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(m_graph_to_screen_projection));
	glBindVertexArray(m_alt_body_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, m_alt_body_vect.size() / 2);  // Last arg is NUMBER OF VERTS!!!!
		
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
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);  //xy
	glEnableVertexAttribArray(0);
	glPointSize(50.0);
	
	m_alt_position_verts[0] = env_config->telemetry_index();
	m_alt_position_verts[1] = env_config->telemetry_slice().m_alt[1];

	// Draw the body.
	glBufferData(GL_ARRAY_BUFFER, sizeof(m_alt_position_verts), m_alt_position_verts, GL_STATIC_DRAW);
	glUniformMatrix4fv(glGetUniformLocation(m_alt_position_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(m_graph_to_screen_projection));
	glBindVertexArray(m_alt_position_vao);
	glDrawArrays(GL_POINTS, 0, 1);  // Last arg is NUMBER OF VERTS!!!!

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void GraphWidget::render() {
	m_shader.use(); 
	WidgetBase::render(m_shader);

	render_alt_body();
	render_alt_outline();
	render_pilot_position();
}
