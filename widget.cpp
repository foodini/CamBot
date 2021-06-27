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

	WidgetBase::render_mask();
	
	//We only need the background mask for this widget.
	//m_shader.use();
	//WidgetBase::render(m_shader);

	char buf[50];
	const TelemetrySlice& ts = env_config->telemetry_slice();
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("Date:   %04d.%02d.%02d", ts.year(), ts.month(), ts.day()),
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.8), glm::vec3(1.0, 1.0, 1.0), 0.33, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("Time:     %02d:%02d:%02d", ts.hour(), ts.minute(), ts.second()),
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.5), glm::vec3(1.0, 1.0, 1.0), 0.33, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));

	float total_sec = env_config->flight_time();
	strcpy(buf, total_sec >= 0.0 ? "Duration +" : "Duration -");
	ffsw::make_time(&buf[strlen(buf)], total_sec, false);
	env_config->font_mgr->add_string(
		StringAndProperties(
			buf, 
			0, glm::vec2(m_x_pos + 0.01, m_y_pos + m_height * 0.2), glm::vec3(1.0, 1.0, 1.0), 0.33, 1.0,
			StringAndProperties::V_ALIGN::V_CENTER, StringAndProperties::H_ALIGN::H_LEFT));
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
 			float parametric = (interaction_mgr->mouse_x_pos() - m_x_pos) / m_width;
			EnvConfig::instance->advance_to_parametric(parametric);
		}
	}
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
	last_speed_scale = speed_scale * 0.01 + last_speed_scale * 0.99;
	float widget_dimension_scale = max_clip.x - min_clip.x;
	glm::vec3 scale_vec(0.01 * last_speed_scale * widget_dimension_scale);  // 0.01 to make the widget 200m tall when pilot stationary.
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

void MapWidget::render() {
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
			0, glm::vec2(m_x_pos, m_y_pos), glm::vec3(1.0, 1.0, 1.0), 0.33, 1.5));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("%3.0f ", ts.m_course_deg),
			0, glm::vec2(m_x_pos + m_width, m_y_pos), glm::vec3(1.0, 1.0, 1.0), 0.33, 1.5,
			StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_RIGHT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			"o",
			0, glm::vec2(m_x_pos + m_width, m_y_pos + 0.005), glm::vec3(1.0, 1.0, 1.0), 0.25, 1.5,
			StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_RIGHT));
	env_config->font_mgr->add_string(
		StringAndProperties(
			ffsw::format("%+6.1ffpm", ts.m_climb_rate[1]),
			0, glm::vec2(m_x_pos, m_y_pos + m_height), glm::vec3(1, 1, 1), 0.33, 1.5,
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

	float stride = num_slices / (env_config->screen_width() * m_width * 2.0); // Doubling in case we get some antialiasing help.
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
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(0 * sizeof(float)));  //xy
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
	WidgetBase::render_mask();

	//m_shader.use(); 
	//WidgetBase::render(m_shader);

	render_alt_body();
	render_alt_outline();
	render_pilot_position();
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