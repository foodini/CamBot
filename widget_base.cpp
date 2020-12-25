#include "widget_base.h"
#include "util.h"

WidgetBase::WidgetBase(float width, float height, float x_pos, float y_pos,
	const std::string& vert, const std::string& frag) :
	m_width(width),
	m_height(height),
	m_x_pos(x_pos),
	m_y_pos(y_pos),
	m_shader(vert.c_str(), frag.c_str())
{
	this->change_geometry(m_width, m_height, m_x_pos, m_y_pos);
}

WidgetBase::~WidgetBase() {

}


// DOES NOT CALL shader.use(). You'll want to do that first - this'll give you the opportunity to set your uniforms.
// All this does is render a single vert/frag shader pair to the quad given in the constructor. 
void WidgetBase::render(Shader& shader) {
	const EnvConfig* env_config = EnvConfig::instance;

	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glBindVertexArray(m_VAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(m_vertices), m_vertices, GL_DYNAMIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(env_config->screen_width()), 0.0f, static_cast<float>(env_config->screen_height()));

	glUniformMatrix4fv(glGetUniformLocation(shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

	glBindVertexArray(m_VAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void WidgetBase::change_geometry(float width, float height, float x_pos, float y_pos) {
	//TODO: CLEAN UP THE STUFF YOU CREATED THE LAST TIME THROUGH
	
	m_width = width;
	m_height = height;
	m_x_pos = x_pos;
	m_y_pos = y_pos;

	for (int i = 0; i < 12; i++)
		m_vertices[i] = 0.0;

	m_vertices[0] = m_x_pos;
	m_vertices[1] = m_y_pos;
	m_vertices[3] = m_x_pos + m_width;
	m_vertices[4] = m_y_pos;
	m_vertices[6] = m_x_pos;
	m_vertices[7] = m_y_pos + m_height;
	m_vertices[9] = m_x_pos + m_width;
	m_vertices[10] = m_y_pos + m_height;

	glGenVertexArrays(1, &m_VAO);
	glGenBuffers(1, &m_VBO);
	glBindVertexArray(m_VAO);

	float screen_width = EnvConfig::instance->screen_width();
	float screen_height = EnvConfig::instance->screen_height();
}
