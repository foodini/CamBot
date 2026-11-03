#pragma once

// An off-screen picture to draw into: the frame as it is made (the video, the telemetry drawn
// over it, and for now the scrub bars under it) before anything is shown. Drawing there instead
// of straight into the window means the frame that gets exported, and saved as a screenshot, is
// no longer tied to the size or the state of the window.
//
// Pixels are 8-bit RGBA, with the same layout as the window's own framebuffer (row 0 at the
// bottom), so coordinates mean the same here as they did when drawing into the window.

#include <stdint.h>

#include <glad/glad.h>

class RenderTarget {
public:
	RenderTarget();
	~RenderTarget();

	// Makes the picture, needs the OpenGL context. False, with nothing made, if the graphics
	// driver can't (the size is too big, or no memory is left).
	bool create(int width, int height);
	// Frees it. Call this before the OpenGL context goes away.
	void destroy();
	bool valid() const { return m_framebuffer != 0; }

	int    width() const   { return m_width; }
	int    height() const  { return m_height; }
	// The picture as a texture, for showing it somewhere (an ImGui image, say).
	GLuint texture() const { return m_texture; }

	// Draws from now on go into the picture (and glReadPixels reads from it) until bind_window().
	// The viewport is not touched: it's the caller's to set.
	void bind() const;
	static void bind_window();

	// Sets every pixel's alpha to 1, leaving the colors alone. Drawing translucent things (the
	// widgets) into the picture leaves it translucent where they were, which makes no difference
	// to a window or to the RGB that is exported, but would make the picture fade into whatever
	// is behind it when it is shown with blending (as the preview is). Call it once the frame is
	// drawn. Binds the picture, and leaves the window's framebuffer bound.
	void make_opaque() const;

	// Reads the rectangle at (x, y) of `width` by `height` pixels as 8-bit RGB, rows from the
	// bottom up (as glReadPixels gives them), no padding between rows. `out` must have room for
	// width * height * 3 bytes.
	void read_rgb(int x, int y, int width, int height, uint8_t* out) const;

private:
	RenderTarget(const RenderTarget&);
	RenderTarget& operator=(const RenderTarget&);

	GLuint m_framebuffer;
	GLuint m_texture;
	int    m_width;
	int    m_height;
};
