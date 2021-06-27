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


// TODO(P2): getters and setters?
class StringAndProperties {
public:
	enum class V_ALIGN { V_TOP, V_CENTER, V_BOTTOM };
	enum class H_ALIGN { H_LEFT, H_CENTER, H_RIGHT };

	StringAndProperties(const std::string& _str, int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale,
		             float _dropshadow, 
		             StringAndProperties::V_ALIGN _v_align, StringAndProperties::H_ALIGN _h_align) :
		str(_str),
		ttl(_ttl),
		pos(_pos),
		color(_color),
		scale(_scale),
		dropshadow(_dropshadow),
		v_align(_v_align),
		h_align(_h_align)
	{}
	
	StringAndProperties(int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale, float _dropshadow, 
		                StringAndProperties::V_ALIGN _v_align, StringAndProperties::H_ALIGN _h_align) :
		StringAndProperties(std::string(), _ttl, _pos, _color, _scale, _dropshadow, _v_align, _h_align)
	{}

	StringAndProperties(const std::string& _str, int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale,
		                float _dropshadow, V_ALIGN _v_align) :
		StringAndProperties(_str, _ttl, _pos, _color, _scale, _dropshadow, _v_align, StringAndProperties::H_ALIGN::H_LEFT)
	{}
	
	StringAndProperties(int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale, float _dropshadow,
		                StringAndProperties::V_ALIGN _v_align) :
		StringAndProperties(std::string(), _ttl, _pos, _color, _scale, _dropshadow, _v_align, StringAndProperties::H_ALIGN::H_LEFT)
	{}

	StringAndProperties(const std::string& _str, int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale,
		                float _dropshadow) :
		StringAndProperties(_str, _ttl, _pos, _color, _scale, _dropshadow, StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT)
	{}
	
	StringAndProperties(int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale, float _dropshadow) :
		StringAndProperties(std::string(), _ttl, _pos, _color, _scale, _dropshadow, StringAndProperties::V_ALIGN::V_BOTTOM,
			                StringAndProperties::H_ALIGN::H_LEFT)
	{}

	StringAndProperties(const std::string& _str, int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale) :
		StringAndProperties(_str, _ttl, _pos, _color, _scale, 0.0, StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT)
	{}

	StringAndProperties(int _ttl, const glm::vec2& _pos, const glm::vec3& _color, float _scale) :
		StringAndProperties(std::string(), _ttl, _pos, _color, _scale, 0.0, StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT)
	{}

	StringAndProperties(const std::string& _str, int _ttl, const glm::vec2& _pos, const glm::vec3& _color) :
		StringAndProperties(_str, _ttl, _pos, _color, 1.0, 0.0, StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT)
	{}

	StringAndProperties(int _ttl, const glm::vec2& _pos, const glm::vec3& _color) :
		StringAndProperties(std::string(), _ttl, _pos, _color, 1.0, 0.0, StringAndProperties::V_ALIGN::V_BOTTOM, StringAndProperties::H_ALIGN::H_LEFT)
	{}

	std::string      str;
	int              ttl;
	glm::vec2        pos;
	glm::vec3        color;
	float            scale;   //TODO(P1) make this the point size.
	float            dropshadow;
	V_ALIGN          v_align;
	H_ALIGN          h_align;
};

class FontManager {
public:
	FontManager(const char * font_path, int size, int screen_width, int screen_height);
	~FontManager();
	void render();
	void add_string(const StringAndProperties& string_properties);
	void update_time(uint64_t timestamp);
	void purge_expired_strings();

private:
	typedef std::pair<StringAndProperties, std::string> QueuePair;
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
	std::list<StringAndProperties>      m_strings;
	uint64_t                            m_time;
	int                                 m_size;

	unsigned int                        m_VAO;
	unsigned int                        m_VBO;
	
	const Character & get_character_struct(char glyph);
	void render_string(const StringAndProperties& string_properties);
	float get_string_width(const std::string& str);
};