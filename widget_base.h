#pragma once

#include <list>
#include <string>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "env_config.h"
#include "shader_s.h"
#include "telemetry.h"

class WidgetBase {
public:
	WidgetBase(float width, float height, float x_pos, float y_pos,
		       const std::string& vert, const std::string& frag);
	virtual ~WidgetBase();

	virtual void handle_input() {}
	virtual void render(Shader& shader);
	virtual void change_geometry(float width, float height, float x_pos, float y_pos);
	 
protected:
	unsigned int m_VBO;
	unsigned int m_VAO;

	float m_width;
	float m_height;
	float m_x_pos;
	float m_y_pos;

	Shader   m_shader;
	float m_vertices[12];

private:

};