#include "path_overlay.h"

#include <stdio.h>

namespace {

// The stroke's width and the dark edge round it, as fractions of the canvas's height (0.003 is 3 pixels of 1080); never less than a pixel.
const double LINE_WIDTH = 0.003;
const double EDGE_WIDTH = 0.001;
const float  EDGE_OPACITY = 0.78f;

// Two triangles covering the canvas, made from the vertex number so there is no buffer to keep. The fragment shader finds how far each
// pixel is from the ring or the line and covers the pixels within the stroke's half width, with a pixel of softness at the edge.
const char* const VERTEX_SOURCE =
	"#version 330 core\n"
	"void main() {\n"
	"    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
	"    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

const char* const FRAGMENT_SOURCE =
	"#version 330 core\n"
	"uniform int   kind;\n"           // 0: a ring about a, of radius b.x; 1: a line from a to b.
	"uniform vec2  a;\n"
	"uniform vec2  b;\n"
	"uniform float half_width;\n"
	"uniform vec4  rgba;\n"
	"out vec4 color;\n"
	"void main() {\n"
	"    vec2 p = gl_FragCoord.xy;\n"
	"    float distance_from_stroke;\n"
	"    if (kind == 0) {\n"
	"        distance_from_stroke = abs(length(p - a) - b.x);\n"
	"    } else {\n"
	"        vec2 along = b - a;\n"
	"        float t = dot(along, along) > 0.0 ? clamp(dot(p - a, along) / dot(along, along), 0.0, 1.0) : 0.0;\n"
	"        distance_from_stroke = length(p - (a + along * t));\n"
	"    }\n"
	"    float coverage = clamp(half_width + 0.5 - distance_from_stroke, 0.0, 1.0);\n"
	"    if (coverage <= 0.0)\n"
	"        discard;\n"
	"    color = vec4(rgba.rgb, rgba.a * coverage);\n"
	"}\n";

GLuint compile(GLenum type, const char* source) {
	const GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("Path overlay: shader would not compile: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

}  // namespace

PathOverlay::PathOverlay()
	: m_program(0), m_vao(0), m_kind_location(-1), m_a_location(-1), m_b_location(-1), m_half_width_location(-1), m_color_location(-1),
	  m_width(0), m_height(0), m_cull_was_on(false) {
	const GLuint vertex = compile(GL_VERTEX_SHADER, VERTEX_SOURCE);
	const GLuint fragment = compile(GL_FRAGMENT_SHADER, FRAGMENT_SOURCE);
	if (vertex != 0 && fragment != 0) {
		m_program = glCreateProgram();
		glAttachShader(m_program, vertex);
		glAttachShader(m_program, fragment);
		glLinkProgram(m_program);
		GLint ok = 0;
		glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
		if (!ok) {
			printf("Path overlay: shader would not link, so circles and leader lines will not show.\n");
			glDeleteProgram(m_program);
			m_program = 0;
		}
		else {
			m_kind_location = glGetUniformLocation(m_program, "kind");
			m_a_location = glGetUniformLocation(m_program, "a");
			m_b_location = glGetUniformLocation(m_program, "b");
			m_half_width_location = glGetUniformLocation(m_program, "half_width");
			m_color_location = glGetUniformLocation(m_program, "rgba");
		}
	}
	if (vertex != 0)
		glDeleteShader(vertex);
	if (fragment != 0)
		glDeleteShader(fragment);
	glGenVertexArrays(1, &m_vao);
}

PathOverlay::~PathOverlay() {
	if (m_program != 0)
		glDeleteProgram(m_program);
	if (m_vao != 0)
		glDeleteVertexArrays(1, &m_vao);
}

void PathOverlay::begin(int canvas_width, int canvas_height) {
	m_width = canvas_width;
	m_height = canvas_height;
	m_cull_was_on = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m_program);
	glBindVertexArray(m_vao);
}

void PathOverlay::end() {
	glBindVertexArray(0);
	if (m_cull_was_on)
		glEnable(GL_CULL_FACE);
}

void PathOverlay::stroke(bool is_ring, double a_x, double a_y, double b_x_or_radius, double b_y, double half_width, const float rgba[4]) {
	glUniform1i(m_kind_location, is_ring ? 0 : 1);
	glUniform2f(m_a_location, (float)a_x, (float)a_y);
	glUniform2f(m_b_location, (float)b_x_or_radius, (float)b_y);
	glUniform1f(m_half_width_location, (float)half_width);
	glUniform4f(m_color_location, rgba[0], rgba[1], rgba[2], rgba[3]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void PathOverlay::ring(double x, double y, double radius, const float color[3], float opacity) {
	if (m_program == 0 || opacity <= 0.0f)
		return;
	if (opacity > 1.0f)
		opacity = 1.0f;
	// The canvas's rows count up from the bottom, as the pixels do.
	const double center_x = x * (double)m_width;
	const double center_y = (1.0 - y) * (double)m_height;
	const double pixels = radius * (double)m_height;
	double half = LINE_WIDTH * (double)m_height * 0.5;
	if (half < 0.5)
		half = 0.5;
	const double edge = EDGE_WIDTH * (double)m_height < 1.0 ? 1.0 : EDGE_WIDTH * (double)m_height;
	const float dark[4] = { 0.0f, 0.0f, 0.0f, EDGE_OPACITY * opacity };
	const float light[4] = { color[0], color[1], color[2], opacity };
	stroke(true, center_x, center_y, pixels, 0.0, half + edge, dark);
	stroke(true, center_x, center_y, pixels, 0.0, half, light);
}

void PathOverlay::line(double from_x, double from_y, double to_x, double to_y, const float color[3], float opacity) {
	if (m_program == 0 || opacity <= 0.0f)
		return;
	if (opacity > 1.0f)
		opacity = 1.0f;
	const double ax = from_x * (double)m_width;
	const double ay = (1.0 - from_y) * (double)m_height;
	const double bx = to_x * (double)m_width;
	const double by = (1.0 - to_y) * (double)m_height;
	double half = LINE_WIDTH * (double)m_height * 0.5;
	if (half < 0.5)
		half = 0.5;
	const double edge = EDGE_WIDTH * (double)m_height < 1.0 ? 1.0 : EDGE_WIDTH * (double)m_height;
	const float dark[4] = { 0.0f, 0.0f, 0.0f, EDGE_OPACITY * opacity };
	const float light[4] = { color[0], color[1], color[2], opacity };
	stroke(false, ax, ay, bx, by, half + edge, dark);
	stroke(false, ax, ay, bx, by, half, light);
}
