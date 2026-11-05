#include "text_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include FT_SYNTHESIS_H

#include "text_fonts.h"
#include "text_layout.h"

namespace {

const size_t MAX_GLYPHS = 4000;       // When the cache holds more than this at the start of a frame, it is emptied.
const int    MIN_PIXEL_SIZE = 4;
const int    MAX_PIXEL_SIZE = 1500;

std::string find_fonts_directory() {
	const char* windows = getenv("WINDIR");
	std::string directory = (windows != NULL && windows[0] != '\0') ? windows : "C:\\Windows";
	return directory + "\\Fonts\\";
}

uint64_t glyph_key(int face_key, int pixel_size, uint32_t code_point) {
	return ((uint64_t)face_key << 40) | ((uint64_t)pixel_size << 22) | (uint64_t)(code_point & 0x3FFFFF);
}

int face_key(int font, bool bold, bool italic) {
	return font * 4 + (bold ? 2 : 0) + (italic ? 1 : 0);
}

}  // namespace

TextRenderer::TextRenderer()
	: m_library(NULL), m_fonts_directory(find_fonts_directory()), m_shader("text.vert", "text.frag"), m_vao(0), m_vbo(0),
	  m_solid_texture(0), m_width(0), m_height(0), m_cull_was_on(false) {
	if (FT_Init_FreeType(&m_library) != 0) {
		printf("Text overlays: could not start FreeType, so no text will be drawn.\n");
		m_library = NULL;
	}

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	const unsigned char white = 255;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &m_solid_texture);
	glBindTexture(GL_TEXTURE_2D, m_solid_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

TextRenderer::~TextRenderer() {
	clear_glyphs();
	for (std::map<int, Face>::iterator it = m_faces.begin(); it != m_faces.end(); ++it)
		FT_Done_Face(it->second.face);
	m_faces.clear();
	if (m_library != NULL)
		FT_Done_FreeType(m_library);
	glDeleteTextures(1, &m_solid_texture);
	glDeleteVertexArrays(1, &m_vao);
	glDeleteBuffers(1, &m_vbo);
}

void TextRenderer::clear_glyphs() {
	for (std::map<uint64_t, Glyph>::iterator it = m_glyphs.begin(); it != m_glyphs.end(); ++it) {
		if (it->second.texture != 0)
			glDeleteTextures(1, &it->second.texture);
	}
	m_glyphs.clear();
}

TextRenderer::Face* TextRenderer::get_face(int font, bool bold, bool italic) {
	if (font < 0 || font >= text_fonts::font_count())
		font = 0;
	const int key = face_key(font, bold, italic);
	std::map<int, Face>::iterator found = m_faces.find(key);
	if (found != m_faces.end())
		return &found->second;
	if (m_library == NULL || m_face_failed.count(key) != 0)
		return NULL;

	// The font's own file; failing that, Arial's.
	Face face;
	face.key = key;
	face.face = NULL;
	face.pixel_size = 0;
	text_fonts::Face wanted = text_fonts::choose_face(font, bold, italic);
	bool loaded = FT_New_Face(m_library, (m_fonts_directory + wanted.file).c_str(), 0, &face.face) == 0;
	if (!loaded && font != 0) {
		printf("Text overlays: could not load %s from %s; using Arial instead.\n", wanted.file.c_str(), m_fonts_directory.c_str());
		wanted = text_fonts::choose_face(0, bold, italic);
		loaded = FT_New_Face(m_library, (m_fonts_directory + wanted.file).c_str(), 0, &face.face) == 0;
	}
	if (!loaded) {
		printf("Text overlays: could not load %s from %s, so this text won't be drawn.\n", wanted.file.c_str(), m_fonts_directory.c_str());
		m_face_failed[key] = true;
		return NULL;
	}
	face.synth_bold = wanted.synth_bold;
	face.synth_italic = wanted.synth_italic;
	std::pair<std::map<int, Face>::iterator, bool> added = m_faces.insert(std::make_pair(key, face));
	return &added.first->second;
}

const TextRenderer::Glyph* TextRenderer::get_glyph(Face* face, int pixel_size, uint32_t code_point) {
	// Tabs are spaces, and the other control characters (the line breaks among them) have nothing to draw.
	if (code_point == '\t')
		code_point = ' ';
	const uint64_t key = glyph_key(face->key, pixel_size, code_point);
	std::map<uint64_t, Glyph>::iterator found = m_glyphs.find(key);
	if (found != m_glyphs.end())
		return &found->second;

	Glyph glyph;
	glyph.texture = 0;
	glyph.width = 0;
	glyph.height = 0;
	glyph.left = 0;
	glyph.top = 0;
	glyph.advance = 0.0f;
	glyph.index = 0;
	if (code_point >= 0x20 && !(code_point >= 0x7F && code_point < 0xA0)) {
		if (face->pixel_size != pixel_size) {
			FT_Set_Pixel_Sizes(face->face, 0, pixel_size);
			face->pixel_size = pixel_size;
		}
		glyph.index = FT_Get_Char_Index(face->face, code_point);
		// Unhinted, so the shapes and the advances scale smoothly with the size; outlines, as the fakes need them.
		if (FT_Load_Glyph(face->face, glyph.index, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0) {
			FT_GlyphSlot slot = face->face->glyph;
			if (slot->format == FT_GLYPH_FORMAT_OUTLINE) {
				if (face->synth_bold)
					FT_GlyphSlot_Embolden(slot);
				if (face->synth_italic)
					FT_GlyphSlot_Oblique(slot);
			}
			glyph.advance = (float)slot->advance.x / 64.0f;
			if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) == 0 && slot->bitmap.width > 0 && slot->bitmap.rows > 0) {
				glyph.width = (int)slot->bitmap.width;
				glyph.height = (int)slot->bitmap.rows;
				glyph.left = slot->bitmap_left;
				glyph.top = slot->bitmap_top;
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glGenTextures(1, &glyph.texture);
				glBindTexture(GL_TEXTURE_2D, glyph.texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, glyph.width, glyph.height, 0, GL_RED, GL_UNSIGNED_BYTE, slot->bitmap.buffer);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
			}
		}
	}
	std::pair<std::map<uint64_t, Glyph>::iterator, bool> added = m_glyphs.insert(std::make_pair(key, glyph));
	return &added.first->second;
}

void TextRenderer::begin(int canvas_width, int canvas_height) {
	m_width = canvas_width;
	m_height = canvas_height;
	if (m_glyphs.size() > MAX_GLYPHS)
		clear_glyphs();

	m_cull_was_on = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const glm::mat4 projection = glm::ortho(0.0f, (float)canvas_width, 0.0f, (float)canvas_height);
	m_shader.use();
	glUniformMatrix4fv(glGetUniformLocation(m_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(m_vao);
}

void TextRenderer::end() {
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	if (m_cull_was_on)
		glEnable(GL_CULL_FACE);
}

// A quad with its lower left corner at (x, y) in canvas pixels (y up), textured with the whole of `texture`.
void TextRenderer::draw_quad(float x, float y, float w, float h, unsigned int texture) {
	const float vertices[6][4] = {
		{ x,     y + h, 0.0f, 0.0f },
		{ x,     y,     0.0f, 1.0f },
		{ x + w, y,     1.0f, 1.0f },

		{ x,     y + h, 0.0f, 0.0f },
		{ x + w, y,     1.0f, 1.0f },
		{ x + w, y + h, 1.0f, 0.0f }
	};
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
}

void TextRenderer::draw_pass(const TextBox& box, const Layout& layout, bool shadow, float alpha) {
	const TextStyle& style = box.style;
	const float* color = shadow ? style.shadow_color : style.color;
	const float pass_alpha = shadow ? alpha * style.shadow_opacity : alpha;
	if (pass_alpha <= 0.0f)
		return;
	glUniform3f(glGetUniformLocation(m_shader.ID, "textColor"), color[0], color[1], color[2]);
	glUniform1f(glGetUniformLocation(m_shader.ID, "textAlpha"), pass_alpha);

	// The shadow's offset is in fractions of the picture's height, down and right being positive; the canvas counts y upward.
	const float shift_x = shadow ? style.shadow_dx * (float)m_height : 0.0f;
	const float shift_y = shadow ? -style.shadow_dy * (float)m_height : 0.0f;
	const float box_left = box.x * (float)m_width;
	const float box_top = (1.0f - box.y) * (float)m_height;          // In canvas pixels, y up.
	const float box_width = box.w * (float)m_width;
	const float strike_thickness = fmaxf(1.0f, roundf((float)layout.pixel_size / 18.0f));

	for (size_t i = 0; i < layout.lines.size(); i++) {
		const text_layout::Line& line = layout.lines[i];
		const float baseline = roundf(box_top - layout.ascent - (float)i * layout.line_height) + shift_y;
		const float start = box_left + text_layout::line_offset(style.align, box_width, line.width);
		float pen = start;
		for (size_t k = line.begin; k < line.end; k++) {
			const Glyph* glyph = layout.glyphs[k];
			if (glyph->texture != 0) {
				const float gx = roundf(pen + (float)glyph->left) + shift_x;
				const float gy = baseline - (float)(glyph->height - glyph->top);
				draw_quad(gx, gy, (float)glyph->width, (float)glyph->height, glyph->texture);
			}
			pen += layout.advances[k];
		}
		if (style.strike && line.width > 0.0f) {
			const float y = baseline + roundf((float)layout.pixel_size * 0.3f) - strike_thickness * 0.5f;
			draw_quad(roundf(start) + shift_x, roundf(y), roundf(line.width), strike_thickness, m_solid_texture);
		}
	}
}

void TextRenderer::draw(const TextBox& box, float alpha) {
	if (alpha <= 0.0f || box.text.empty() || m_width <= 0 || m_height <= 0)
		return;
	const TextStyle& style = box.style;
	Face* face = get_face(style.font, style.bold, style.italic);
	if (face == NULL)
		return;

	Layout layout;
	int pixel_size = (int)roundf(style.size * (float)m_height);
	if (pixel_size < MIN_PIXEL_SIZE)
		pixel_size = MIN_PIXEL_SIZE;
	if (pixel_size > MAX_PIXEL_SIZE)
		pixel_size = MAX_PIXEL_SIZE;
	layout.pixel_size = pixel_size;

	// Setting the size here as well as when a glyph is drawn: the metrics and kerning below are in the face's current size.
	if (face->pixel_size != pixel_size) {
		FT_Set_Pixel_Sizes(face->face, 0, pixel_size);
		face->pixel_size = pixel_size;
	}
	layout.ascent = (float)face->face->size->metrics.ascender / 64.0f;
	layout.line_height = (float)face->face->size->metrics.height / 64.0f;
	if (layout.line_height < 1.0f)
		layout.line_height = (float)pixel_size * 1.2f;

	layout.code_points = text_layout::decode_utf8(box.text);
	const size_t count = layout.code_points.size();
	layout.glyphs.reserve(count);
	for (size_t i = 0; i < count; i++)
		layout.glyphs.push_back(get_glyph(face, pixel_size, layout.code_points[i]));
	layout.advances.reserve(count);
	for (size_t i = 0; i < count; i++) {
		float advance = layout.glyphs[i]->advance;
		if (i + 1 < count && FT_HAS_KERNING(face->face) && layout.glyphs[i]->index != 0 && layout.glyphs[i + 1]->index != 0) {
			FT_Vector kerning;
			if (FT_Get_Kerning(face->face, layout.glyphs[i]->index, layout.glyphs[i + 1]->index, FT_KERNING_UNFITTED, &kerning) == 0)
				advance += (float)kerning.x / 64.0f;
		}
		layout.advances.push_back(advance);
	}
	layout.lines = text_layout::wrap(layout.code_points, layout.advances, box.w * (float)m_width);

	draw_pass(box, layout, true, alpha);     // The shadow first, so the text lies on it.
	draw_pass(box, layout, false, alpha);
}
