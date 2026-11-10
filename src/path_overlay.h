#pragma once

// Draws what goes with a path onto the canvas: the circle round the target, and the leader line from a text box to it (see
// tracked_path.h and path_attachment.h). Both are in the path's circle colour, over a darker edge so they show on any picture, and
// fade by an opacity. Sizes are fractions of the canvas's height, so the preview, the export and the saved frames agree.
//
// Use: begin() once a frame, ring() and line() for each, then end(). Draw them before the text, so text lies over them. Needs an
// OpenGL context, and the canvas bound with its viewport set to the whole of it.

#include <glad/glad.h>

class PathOverlay {
public:
	PathOverlay();
	~PathOverlay();

	void begin(int canvas_width, int canvas_height);
	// A circle of `radius` (a fraction of the canvas's height) about the place `x`, `y` (fractions of its width and height from the top
	// left), `opacity` 0 to 1 (nothing is drawn at 0 or less).
	void ring(double x, double y, double radius, const float color[3], float opacity);
	// A line between two such places.
	void line(double from_x, double from_y, double to_x, double to_y, const float color[3], float opacity);
	void end();

private:
	PathOverlay(const PathOverlay&);
	PathOverlay& operator=(const PathOverlay&);

	// Draws one primitive (a ring if `is_ring`, else a line) as a stroke `half_width` pixels either side, in `rgba`.
	void stroke(bool is_ring, double a_x, double a_y, double b_x_or_radius, double b_y, double half_width, const float rgba[4]);

	GLuint m_program;
	GLuint m_vao;
	GLint  m_kind_location;
	GLint  m_a_location;
	GLint  m_b_location;
	GLint  m_half_width_location;
	GLint  m_color_location;
	int    m_width;
	int    m_height;
	bool   m_cull_was_on;
};
