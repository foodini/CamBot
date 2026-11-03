#include "render_target.h"

RenderTarget::RenderTarget() : m_framebuffer(0), m_texture(0), m_width(0), m_height(0) {
}

RenderTarget::~RenderTarget() {
	destroy();
}

bool RenderTarget::create(int width, int height) {
	destroy();
	if (width <= 0 || height <= 0)
		return false;

	glGenTextures(1, &m_texture);
	glBindTexture(GL_TEXTURE_2D, m_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);   // Leave the texture unit as found.

	glGenFramebuffers(1, &m_framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		destroy();
		return false;
	}
	m_width = width;
	m_height = height;
	return true;
}

void RenderTarget::destroy() {
	if (m_framebuffer != 0)
		glDeleteFramebuffers(1, &m_framebuffer);
	if (m_texture != 0)
		glDeleteTextures(1, &m_texture);
	m_framebuffer = 0;
	m_texture = 0;
	m_width = 0;
	m_height = 0;
}

void RenderTarget::bind() const {
	glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
}

void RenderTarget::bind_window() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::make_opaque() const {
	glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::read_rgb(int x, int y, int width, int height, uint8_t* out) const {
	GLint previous_read = 0;
	glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
	GLint previous_alignment = 4;
	glGetIntegerv(GL_PACK_ALIGNMENT, &previous_alignment);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_framebuffer);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, (void*)out);

	glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read);
}
