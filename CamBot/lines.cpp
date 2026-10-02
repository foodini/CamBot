#include "lines.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"


Lines::Lines(float line_width, float dot_frequency) :
	m_line_width(line_width),
	m_dot_frequency(dot_frequency),
	m_remaining_dist(0.0),
	m_vert_count(0),
	m_dot_vector()
{
	glGenVertexArrays(1, &m_VAO);
	glGenBuffers(1, &m_VBO);
	glBindVertexArray(m_VAO);
}

void Lines::push_vert(glm::vec2& xy, std::vector<float>& start_vals, std::vector<float>& end_vals, float t) {
	m_dot_vector.push_back(xy.x);
	m_dot_vector.push_back(xy.y);

#ifdef _DEBUG
	assert(start_vals.size() == end_vals.size());
#endif

	auto start_val = start_vals.begin();
	auto end_val = end_vals.begin();
	while (start_val != start_vals.end()) {
		float sv = *start_val;
		float ev = *end_val;
		m_dot_vector.push_back(sv + t * (ev - sv));
		start_val++;
		end_val++;
	}
	m_vert_count++;
}

void Lines::add_segment(glm::vec2& xy, std::vector<float>& supplementary) {
	if (m_dot_vector.size() == 0) {
		push_vert(xy, supplementary, supplementary, 0.0);
		m_remaining_dist = 0.0;
		m_last_target = xy;
		m_last_supplementary = supplementary;
		return;
	}

	// We need to run along the vector from m_last_target to xy, dropping a new dot every m_dot_frequency
	// units of distance, until we cannot procede further w/o overrunning xy.
	float remaining_dist = m_dot_frequency - m_remaining_dist;
	glm::vec2 dir = xy - m_last_target;
	float dir_len = glm::length(dir);
	if (remaining_dist > dir_len) {
		remaining_dist -= dir_len;
		m_last_target = xy;
		m_last_supplementary = supplementary;
		return;
	}

	float traveled_dist = remaining_dist;
	glm::vec2 new_vert;
	float t;
	while (true) {
		t = traveled_dist / dir_len;
		new_vert = m_last_target + t * dir;
		push_vert(new_vert, m_last_supplementary, supplementary, t);
		if (traveled_dist + m_dot_frequency > dir_len) {
			m_remaining_dist = dir_len - traveled_dist;
			break;
		}
		traveled_dist += m_dot_frequency;
	}
	m_last_target = xy;
	m_last_supplementary = supplementary;
}

// MAKE SURE you call shader.use() (and set your uniforms) on your shader before calling this thing.
// When use() is run, it wipes any old values, so I changed render() to not call use() and depend
// upon the caller to do so.
void Lines::render(void(*set_vertex_attribs)()) {
	if (m_dot_vector.size() == 0)
		return;

	glEnable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindVertexArray(m_VAO);

	glPointSize(m_line_width);

	glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_dot_vector.size(), m_dot_vector.data(), GL_DYNAMIC_DRAW);

	if (set_vertex_attribs) {
		(*set_vertex_attribs)();
	} else {
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(0 * sizeof(float)));
		glEnableVertexAttribArray(0);
	}

	glBindVertexArray(m_VAO);
	glDrawArrays(GL_POINTS, 0, m_vert_count);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}