#pragma once

#include <list>
#include <map>
#include <tuple>
#include <string>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "glm/glm.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "shader_s.h"

#define FM_TUPLE_STR 0
#define FM_TUPLE_TTL 1
#define FM_TUPLE_SCALE 2
#define FM_TUPLE_POS 3
#define FM_TUPLE_COL 4

class FontManager {
public:
	FontManager(const char * font_path, int size, int screen_width, int screen_height);
	~FontManager();
	void render();
	void add_string(const std::string& str, int ttl, float scale, const glm::vec2& pos, const glm::vec3& color, float dropshadow);
	void format(int ttl, float scale, const glm::vec2& pos, const glm::vec3& color, float dropshadow, const char* fmt, ...);
	void update_time(uint64_t timestamp);
	void purge_expired_strings();

private:
	typedef std::tuple<std::string, int, float, glm::vec2, glm::vec3> t_string;
	struct Character {
		unsigned int TextureID;
		glm::ivec2 Size;
		glm::ivec2 Bearing;
		unsigned int Advance;
	};
	std::map<char, Character>           m_characters;
	FT_Library                          m_ft_library;
	FT_Face                             m_ft_face;
	Shader                              m_shader;
	std::list<t_string>                 m_strings;
	uint64_t                            m_time;

	unsigned int                        m_VAO;
	unsigned int                        m_VBO;
	
	const Character & get_character_struct(char glyph);
	void render_string(const std::string& str, float scale, const glm::vec2 & pos, const glm::vec3 & color);

};