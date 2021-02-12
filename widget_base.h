#pragma once

#include <list>
#include <string>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "env_config.h"
#include "shader_s.h"
#include "telemetry.h"

class TelemetrySlice;

class WidgetBase {
public:
	//TODO(P0): Should we really be passing in the vert and frag shaders? If so, we should be using a
	//          default shader whenever all we're doing is darkening the background. (The media scrubber
	//          is only one shader that does the darkening and the progress line in one frag.)
	WidgetBase(float width, float height, float x_pos, float y_pos,
		       const std::string& vert, const std::string& frag);
	virtual ~WidgetBase();

	virtual void handle_input() {}
	virtual void render(Shader& shader);
	virtual void change_geometry(float width, float height, float x_pos, float y_pos);
	virtual void polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices) {};

protected:
	unsigned int m_VBO;
	unsigned int m_VAO;

	float      m_width;
	float      m_height;
	float      m_x_pos;
	float      m_y_pos;

	Shader     m_shader;
	float      m_vertices[20]; //(x, y, z, u, v) * 4

private:

};