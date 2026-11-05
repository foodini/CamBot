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

	glm::vec2             latlon_to_coords(double lat, double lon);
	double                m_center_lat;
	double                m_center_lon;

	// The telemetry index range last used to build m_course_lines (see EnvConfig::
	// telemetry_window_start_index()/_end_index()) -- -1 forces an initial build. Rebuilt
	// from TelemetryMgr directly (not through polygonalize()) whenever this changes, i.e.
	// whenever the TSW's offset or cutoffs move.
	int32_t               m_window_start_index = -1;
	int32_t               m_window_end_index = -1;
	// TelemetryMgr::climb_version() the course colors were last built from -- the course bakes
	// each point's climb rate into its vertices, so it rebuilds when the smoothing changes.
	uint32_t              m_climb_version = 0;
	// Likewise TelemetryMgr::position_version(), for the smoothed positions the course is drawn through.
	uint32_t              m_position_version = 0;
	void                  rebuild_course_for_window(int32_t start_index, int32_t end_index);

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

	// The telemetry index range last used to build m_alt_body_vect/m_alt_outline_lines (see
	// EnvConfig::telemetry_window_start_index()/_end_index()) -- -1 forces an initial build.
	// polygonalize() itself is untouched (so the graph still progressively fills in while the
	// file is still parsing); once parsing is done, this rebuild -- driven straight off
	// TelemetryMgr with a stride sized to just the window, not the whole file -- takes over
	// and re-runs whenever the TSW's offset or cutoffs move.
	int32_t            m_window_start_index = -1;
	int32_t            m_window_end_index = -1;
	// TelemetryMgr::altitude_version() the graph was last built from -- it bakes altitudes into
	// geometry, so a change to the altitude references means a rebuild.
	uint32_t           m_alt_version = 0;
	void               rebuild_windowed_data(int32_t start_index, int32_t end_index);
};

class ClimbWidget : public WidgetBase {
public:
	ClimbWidget(float width, float height, float x_pos, float y_pos);
	~ClimbWidget();
	void render();
private:

};
