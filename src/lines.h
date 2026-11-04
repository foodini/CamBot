#pragma once

#include <mutex>
#include <vector>

#include "shader_s.h"

#include "glm/glm.hpp"

class Lines {
public:
	Lines(float line_width, float dot_frequency);
	~Lines(){}

	void add_segment(glm::vec2& xy, std::vector<float>& supplementary);
	// The telemetry parse thread adds segments while the render thread draws, so everything that
	// touches the points takes m_mutex. (There used to be an unlocked get_buffer() that handed out
	// a pointer into the points; nothing used it, and a pointer like that can't be made safe.)
	size_t size() { std::lock_guard<std::mutex> lock(m_mutex); return m_vert_count; } // Returns NUMBER OF VERTS!
	void clear();
	// upload_data = false draws the points uploaded by the previous render() again without
	// re-uploading them -- for drawing the same vertices twice with different shader settings.
	void render(void(*set_vertex_attribs)() = nullptr, bool upload_data = true);
private:
	void push_vert(glm::vec2& xy, std::vector<float>& start_vals, std::vector<float>& end_vals, float t);

	std::mutex           m_mutex;           // Guards everything below down to m_last_supplementary.
	std::vector<float>   m_dot_vector;
	uint32_t             m_vert_count;
	uint32_t             m_uploaded_vert_count;   // How many verts the buffer on the GPU holds.

	float                m_line_width;
	float                m_dot_frequency;

	float                m_remaining_dist; //How much distance between last submitted dot and last target?
	glm::vec2            m_last_target;
	std::vector<float>   m_last_supplementary;

	uint32_t             m_VAO;
	uint32_t             m_VBO;
};