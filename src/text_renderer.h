#pragma once

// Draws text boxes (text_box.h) onto the canvas: the lines of a box, wrapped to its width and lined up the way
// its style says, in its font, bold, italic and colour, with its strikethrough and its drop shadow, all faded by
// a given opacity. This is the one place the text overlays touch FreeType and OpenGL.
//
// It is separate from FontManager, which draws the telemetry widgets' ASCII text in one font: this one reads
// UTF-8, picks from the fonts of text_fonts.h, and sizes its type as a fraction of the picture's height, so
// preview and export match.
//
// Use: begin() once per frame with the canvas's size in pixels, draw() for each box showing (with the opacity
// text_box::alpha_at gives), then end(). Needs an OpenGL context, and the canvas bound.

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <glad/glad.h>
#include "glm/glm.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "shader_s.h"
#include "text_box.h"
#include "text_layout.h"

class TextRenderer {
public:
	TextRenderer();
	~TextRenderer();

	void begin(int canvas_width, int canvas_height);
	// Draws `box` at opacity `alpha` (0 to 1; nothing is drawn at 0).
	void draw(const TextBox& box, float alpha);
	void end();

private:
	struct Face {
		int     key;             // font * 4 + bold * 2 + italic: how m_faces and the glyph cache know it.
		FT_Face face;
		bool    synth_bold;
		bool    synth_italic;
		int     pixel_size;      // What the face is set to now.
	};
	struct Glyph {
		unsigned int texture;    // 0 for a glyph with nothing to draw (a space).
		int          width;
		int          height;
		int          left;       // Where the bitmap sits relative to the pen: right of it, and above the baseline.
		int          top;
		float        advance;    // In pixels.
		FT_UInt      index;      // In the face; 0 if it has none for the character.
	};

	// The face for a font and style, loaded the first time it is asked for; null if it can't be had at all.
	Face* get_face(int font, bool bold, bool italic);
	// The glyph for `code_point` in `face` at `pixel_size`, drawn the first time it is asked for.
	const Glyph* get_glyph(Face* face, int pixel_size, uint32_t code_point);
	void clear_glyphs();
	void draw_quad(float x, float y, float w, float h, unsigned int texture);
	// What a box looks like laid out: its characters and their glyphs, and where the lines break.
	struct Layout {
		std::vector<uint32_t>          code_points;
		std::vector<const Glyph*>      glyphs;
		std::vector<float>             advances;    // Each includes the kerning to the next character.
		std::vector<text_layout::Line> lines;
		int                            pixel_size;
		float                          ascent;      // From the top of a line to its baseline, and from one baseline to the next.
		float                          line_height;
	};
	// One pass over the lines: the text, or its shadow if `shadow`.
	void draw_pass(const TextBox& box, const Layout& layout, bool shadow, float alpha);

	FT_Library                        m_library;
	std::map<int, Face>               m_faces;           // By font and style.
	std::map<int, bool>               m_face_failed;     // Faces that couldn't be loaded, so we say so only once.
	std::map<uint64_t, Glyph>         m_glyphs;
	std::string                       m_fonts_directory;
	Shader                            m_shader;
	unsigned int                      m_vao;
	unsigned int                      m_vbo;
	unsigned int                      m_solid_texture;   // One white pixel, for the strikethrough.
	int                               m_width;
	int                               m_height;
	bool                              m_cull_was_on;
};
