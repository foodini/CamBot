#pragma once

#include <vector>

#include "shader_s.h"

#include "glm/glm.hpp"

class Lines {
public:
	Lines(float line_width, float dot_frequency);
	~Lines(){}

	void add_segment(glm::vec2& xy, std::vector<float>& supplementary);
	//TODO(P0): checking out this buffer ought to lock it against adds, in case the
	//          std::vector frees its underlying buffer (for resizing up).
	const float* get_buffer(uint32_t* size) { *size = m_dot_vector.size(); return m_dot_vector.data(); }

	size_t size() { return m_vert_count; } // Returns NUMBER OF VERTS!
	void clear() { m_dot_vector.clear(); m_vert_count = 0; }
	void render(void(*set_vertex_attribs)() = nullptr);
private:
	void push_vert(glm::vec2& xy, std::vector<float>& start_vals, std::vector<float>& end_vals, float t);

	std::vector<float>   m_dot_vector;
	uint32_t             m_vert_count;

	float                m_line_width;
	float                m_dot_frequency;

	float                m_remaining_dist; //How much distance between last submitted dot and last target?
	glm::vec2            m_last_target;
	std::vector<float>   m_last_supplementary;

	uint32_t             m_VAO;
	uint32_t             m_VBO;
};