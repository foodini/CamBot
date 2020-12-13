#include "stdio.h"

#include "interaction_manager.h"
#include "util.h"
#include "widget.h"

DateTimeWidget::DateTimeWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "date_time_widget.vert", "date_time_widget.frag")
{

}

DateTimeWidget::~DateTimeWidget() {

}

void DateTimeWidget::render() {
	const EnvConfig& env_config = EnvConfig::instance();

	m_shader.use();

	WidgetBase::render(m_shader);

	char buf[50];
	const TelemetrySlice& ts = env_config.telemetry_slice();
	sprintf(buf, "Date:   %04d.%02d.%02d", ts.year(), ts.month(), ts.day());
	env_config.font_mgr().add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 39.0), glm::vec3(1.0, 1.0, 1.0), 1);
	sprintf(buf, "Time:     %02d:%02d:%02d", ts.hour(), ts.minute(), ts.second());
	env_config.font_mgr().add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 22.0), glm::vec3(1.0, 1.0, 1.0), 1);
	float total_sec = env_config.flight_time();
	strcpy(buf, total_sec >= 0.0 ? "Duration +" : "Duration -");
	ffsw::make_time(&buf[strlen(buf)], total_sec, false);
	env_config.font_mgr().add_string(buf, 0, .25, glm::vec2(m_x_pos + 5.0, m_y_pos + 5.0), glm::vec3(1.0, 1.0, 1.0), 1);
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
			EnvConfig::instance().advance_to_parametric(parametric);
		}
	}
}

void MediaScrubWidget::render() {
	const EnvConfig& env_config = EnvConfig::instance();

	// Uniform settings must happen after a use() call
	m_shader.use();
	m_shader.setFloat("xpos", m_x_pos);
	m_shader.setFloat("ypos", m_y_pos);
	m_shader.setFloat("width", m_width);
	m_shader.setFloat("height", m_height);
	m_shader.setFloat("time", env_config.media_in_elapsed());
	m_shader.setFloat("time_parametric", env_config.time_parametric());

	WidgetBase::render(m_shader);

	char buf[50];
	ffsw::make_time(buf, env_config.media_in_elapsed(), true);
	strcpy(&buf[strlen(buf)], " / ");
	ffsw::make_time(&buf[strlen(buf)], env_config.media_in_duration(), true);
	env_config.font_mgr().add_string(buf, 0, .33f, glm::vec2(m_x_pos + 5.0, m_y_pos + 5.0), glm::vec3(1.0, 1.0, 1.0), 1.0f);
}


MapWidget::MapWidget(float width, float height, float x_pos, float y_pos) :
	WidgetBase(width, height, x_pos, y_pos, "map_widget.vert", "map_widget.frag"),
	m_shader_course("map_widget_1_course.vert", "map_widget_1_course.frag"),
	m_shader_arrow("map_widget_2_arrow.vert", "map_widget_2_arrow.frag")
{
	for (int i = 0; i < test_course_buffer_vert_count; i++) {
		test_course_buffer[i * 4 + 0] = i % 2 ? 20.0 : 10.0 + (i % 4);
		test_course_buffer[i * 4 + 1] = 10.0 + (i/2) * 10;
		test_course_buffer[i * 4 + 2] = i % 2;
		test_course_buffer[i * 4 + 3] = 10.0;
	}

	glGenVertexArrays(1, &m_course_vao);
	glGenBuffers(1, &m_course_vbo);
	//glBindVertexArray(m_course_vao);
	//glBindBuffer(GL_ARRAY_BUFFER, m_course_vbo);
	//glBufferData(GL_ARRAY_BUFFER, sizeof(test_course_buffer), test_course_buffer, GL_STATIC_DRAW);
	/*
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);  //xy
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));  // uv
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));  // 1-float color
	glEnableVertexAttribArray(2);
	*/
}

MapWidget::~MapWidget() {
}

void MapWidget::_set_uniforms(Shader& shader) {
	const EnvConfig& env_config = EnvConfig::instance();

	shader.use();
	shader.setFloat("xpos", m_x_pos);
	shader.setFloat("ypos", m_y_pos);
	shader.setFloat("width", m_width);
	shader.setFloat("height", m_height);
	shader.setFloat("time", env_config.media_in_elapsed());
	shader.setFloat("time_parametric", env_config.time_parametric());
	shader.setFloat("course", env_config.telemetry_slice().course_rad());
	shader.setFloat("speed", env_config.telemetry_slice().speed_mph());
}

void MapWidget::set_uniforms() {
	_set_uniforms(m_shader);
	_set_uniforms(m_shader_course);
	_set_uniforms(m_shader_arrow);
}

void MapWidget::render() {
	const EnvConfig& env_config = EnvConfig::instance();

	set_uniforms();
	
	m_shader.use();
	WidgetBase::render(m_shader);
	
	if (env_config.telemetry_mgr().geometry_available()) {
		// Set up geometry for course tracing
		m_shader_course.use();

		glEnable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glBindVertexArray(m_course_vao);
		glBindBuffer(GL_ARRAY_BUFFER, m_course_vbo);

		uint32_t course_buffer_size;
		if (true) {
			float* course_buffer = env_config.telemetry_mgr().get_course_buffer(course_buffer_size);
			glBufferData(GL_ARRAY_BUFFER, sizeof(float) * course_buffer_size, course_buffer, GL_STATIC_DRAW);
		} else {
			course_buffer_size = sizeof(test_course_buffer) / 4;
			glBufferData(GL_ARRAY_BUFFER, sizeof(test_course_buffer), test_course_buffer, GL_STATIC_DRAW);
		}
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);  //xy
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));  // u part of uv
		glEnableVertexAttribArray(1);
		/*
		glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));  // 1-float color
		glEnableVertexAttribArray(2);
		*/

		// The first value is the ID of the attribute in the shader layout
		glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(env_config.screen_width()), 0.0f, static_cast<float>(env_config.screen_height()));

		glUniformMatrix4fv(glGetUniformLocation(m_shader_course.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

		glBindVertexArray(m_course_vao);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, course_buffer_size);

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}

	m_shader_arrow.use();
	WidgetBase::render(m_shader_arrow);

	env_config.font_mgr().format(0, 0.33f, glm::vec2(m_x_pos, m_y_pos), glm::vec3(1.0, 1.0, 1.0), 1.0, "%5.1frad", env_config.telemetry_slice().course_rad());
	env_config.font_mgr().format(0, 0.33f, glm::vec2(m_x_pos, m_y_pos + 16.0), glm::vec3(1.0, 1.0, 1.0), 1.0, "%5.1fdeg", env_config.telemetry_slice().m_course_deg);
}