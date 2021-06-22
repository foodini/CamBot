#pragma once

#include "lines.h"
#include "widget_base.h"

class DateTimeWidget: public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	DateTimeWidget(float width, float height, float x_pos, float y_pos);
	virtual ~DateTimeWidget();

	virtual void render();

protected:
private:
};


class MediaScrubWidget : public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	MediaScrubWidget(float width, float height, float x_pos, float y_pos);
	virtual ~MediaScrubWidget();

	virtual void handle_input();
	virtual void render();

protected:
private:
};

class MapWidget : public WidgetBase {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	MapWidget(float width, float height, float x_pos, float y_pos);
	virtual ~MapWidget();
	void polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices);

	virtual void render();

protected:
private:
	Lines                 m_course_lines;

	void                  render_course();
	void                  set_uniforms();
	void                  _set_uniforms(Shader& shader);

	glm::vec2             latlon_to_coords(float lat, float lon);
	float                 m_center_lat;
	float                 m_center_lon;

	Shader                m_shader_course;
	Shader                m_shader_arrow;

	uint32_t              m_course_vao;
	uint32_t              m_course_vbo;

	std::vector<uint32_t> m_vertex_attrib_sizes;
};

class GraphWidget : public WidgetBase {
public:
	GraphWidget(float width, float height, float x_pos, float y_pos);
	virtual ~GraphWidget();
	void polygonalize(TelemetrySlice& slice, uint32_t index, uint32_t num_slices);

	virtual void render();

protected:
private:
	virtual void render_alt_body();
	virtual void render_alt_outline();
	virtual void render_pilot_position();

	float              m_next_index;
	float              m_alt_min;
	float              m_alt_max;
	// We need a little padding below the lowest altitude so we have some shading under the curve:
	float              m_below_min_alt;

	Shader             m_alt_body_shader;
	std::vector<float> m_alt_body_vect;
	uint32_t           m_alt_body_vao;
	uint32_t           m_alt_body_vbo;

	Shader             m_alt_outline_shader;
	Lines              m_alt_outline_lines;
	uint32_t           m_alt_outline_vao;
	uint32_t           m_alt_outline_vbo;

	Shader             m_alt_position_shader;
	uint32_t           m_alt_position_vao;
	uint32_t           m_alt_position_vbo;
	float              m_alt_position_verts[2];

	void               update_graph_to_screen_projection();
	glm::mat4          m_graph_to_screen_projection;
};

class ClimbWidget : public WidgetBase {
public:
	ClimbWidget(float width, float height, float x_pos, float y_pos);
	~ClimbWidget();
	void render();
private:

};
