#include "fade_overlay.h"

#include <stdio.h>

namespace {

// Two triangles covering the canvas, made from the vertex number so there is no buffer to keep.
const char* const VERTEX_SOURCE =
	"#version 330 core\n"
	"void main() {\n"
	"    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
	"    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

const char* const FRAGMENT_SOURCE =
	"#version 330 core\n"
	"uniform float opacity;\n"
	"out vec4 color;\n"
	"void main() {\n"
	"    color = vec4(0.0, 0.0, 0.0, opacity);\n"
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
		printf("Fade overlay: shader would not compile: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

}  // namespace

FadeOverlay::FadeOverlay() : m_program(0), m_vao(0), m_opacity_location(-1) {
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
			printf("Fade overlay: shader would not link, so fades will not show.\n");
			glDeleteProgram(m_program);
			m_program = 0;
		}
		else {
			m_opacity_location = glGetUniformLocation(m_program, "opacity");
		}
	}
	if (vertex != 0)
		glDeleteShader(vertex);
	if (fragment != 0)
		glDeleteShader(fragment);
	glGenVertexArrays(1, &m_vao);
}

FadeOverlay::~FadeOverlay() {
	if (m_program != 0)
		glDeleteProgram(m_program);
	if (m_vao != 0)
		glDeleteVertexArrays(1, &m_vao);
}

void FadeOverlay::draw(float opacity) {
	if (m_program == 0 || opacity <= 0.0f)
		return;
	if (opacity > 1.0f)
		opacity = 1.0f;
	const bool cull_was_on = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(m_program);
	glUniform1f(m_opacity_location, opacity);
	glBindVertexArray(m_vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
	if (cull_was_on)
		glEnable(GL_CULL_FACE);
}
