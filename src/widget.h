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


// Shared base for the two scrub-bar widgets (MediaScrubWidget and TelemetryScrubWidget): same
// left-drag-to-absolute-position / right-drag-to-scan mouse gestures, same rate curve, just
// applied to a different underlying quantity. A subclass says what its bar's fill position
// means (current_parametric()), what its native 1x rate is (native_rate()), and how to actually
// move (seek_to_parametric()/step()/step_fast()) -- everything else is identical between them.
class ScrubWidget : public WidgetBase {
public:
	ScrubWidget(float width, float height, float x_pos, float y_pos,
	            const std::string& vert, const std::string& frag);
	virtual ~ScrubWidget();

	virtual void handle_input();

protected:
	// [0..1] position to render the bar's fill at.
	virtual float current_parametric() const = 0;
	// "Units" per second at native 1x speed -- video frame rate for MediaScrubWidget,
	// TELEMETRY_FREQUENCY for TelemetryScrubWidget. Scales the scan-rate curve.
	virtual float native_rate() const = 0;
	// Left-click/drag: jump straight to an absolute parametric position.
	virtual void  seek_to_parametric(float parametric) = 0;
	// Right-click scan: precise, one-unit-at-a-time step (the only path this gesture reaches today
	// -- see the per-tick cap in handle_input()).
	virtual void  step(int64_t delta) = 0;
	// Same safety-net slot MediaScrubWidget already had for a larger, approximate step; currently
	// unreachable from this gesture for either subclass, kept for parity / future use.
	virtual void  step_fast(int64_t delta) = 0;

private:
	// Maps a signed horizontal drag offset (NDC units, same space as InteractionMgr::mouse_x_pos())
	// from the right-click scan origin into a signed rate, in native units per second (see
	// native_rate()). Positive = scan forward, negative = scan backward. See widget.cpp for the
	// curve shape.
	float scan_rate_for_offset(float dx) const;

	// Left button: click-and-hold to scrub to an absolute position.
	bool  m_dragging = false;

	// Right button: click-and-drag to scan forward/backward at a variable rate.
	bool  m_scanning = false;
	float m_scan_origin_x = 0.0f;
	float m_scan_frame_accumulator = 0.0f;  // Fractional units carried between calls; sign-aware.
	float m_last_scan_tick_time = 0.0f;
};

class MediaScrubWidget : public ScrubWidget {
public:
	//TODO(P1): make width, height, pos all part of vec2s.
	MediaScrubWidget(float width, float height, float x_pos, float y_pos);
	virtual ~MediaScrubWidget();

	virtual void render();

protected:
	float current_parametric() const override;
	float native_rate() const override;
	void  seek_to_parametric(float parametric) override;
	void  step(int64_t delta) override;
	void  step_fast(int64_t delta) override;
private:
};

// Scrubs the fixed time offset between the video's clock and the telemetry device's clock
// (they aren't hardware-synced -- see TelemetryMgr::offset()). Behaves like MediaScrubWidget --
// its bar's fill position is telemetry's own [0..1] position across its own recorded duration
// -- but moving it never touches the video position, only the offset: dragging to parametric p
// means "telemetry's elapsed time should be p*duration right now," which is solved for a new
// offset rather than for a seek.
class TelemetryScrubWidget : public ScrubWidget {
public:
	TelemetryScrubWidget(float width, float height, float x_pos, float y_pos);
	virtual ~TelemetryScrubWidget();

	virtual void render();

protected:
	float current_parametric() const override;
	float native_rate() const override;
	void  seek_to_parametric(float parametric) override;
	void  step(int64_t delta) override;
	void  step_fast(int64_t delta) override;

private:
	// Clamped [0,1] position of a cutoff marker, read from EnvConfig's launch-marker-derived
	// telemetry_window_start_elapsed() (is_left selects which one: the left cutoff is that
	// value itself, the right is that value plus media_in_duration()). Not interactive -- the
	// markers are drawn purely for feedback; there's no drag path that reaches this. *in_bounds
	// is set to false when the true, unclamped position fell outside [0,1] -- i.e. we've run
	// out of recorded telemetry on that side -- so the caller can draw the clamped marker but
	// still know to render it grey instead of white.
	float cutoff_parametric(bool is_left, bool* in_bounds) const;

	// What offset makes telemetry's elapsed time equal parametric*duration() at the given
	// reference point on the video's own clock. Used by seek_to_parametric() for an ordinary
	// drag.
	static float offset_for_parametric_at(float parametric, float reference_media_elapsed);
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

	// The telemetry index range last used to build m_course_lines (see EnvConfig::
	// telemetry_window_start_index()/_end_index()) -- -1 forces an initial build. Rebuilt
	// from TelemetryMgr directly (not through polygonalize()) whenever this changes, i.e.
	// whenever the TSW's offset or cutoffs move.
	int32_t               m_window_start_index = -1;
	int32_t               m_window_end_index = -1;
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
	void               rebuild_windowed_data(int32_t start_index, int32_t end_index);
};

class ClimbWidget : public WidgetBase {
public:
	ClimbWidget(float width, float height, float x_pos, float y_pos);
	~ClimbWidget();
	void render();
private:

};
