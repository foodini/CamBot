
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "env_config.h"
#include "font_manager.h"

FontManager::FontManager(const char * font_file, int size, int screen_width, int screen_height) :
    m_shader("text.vert", "text.frag"),
    m_strings(),
    m_time(0),
    m_size(size)
{
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(screen_width), 0.0f, static_cast<float>(screen_height));
    m_shader.use();
    glUniformMatrix4fv(glGetUniformLocation(m_shader.ID, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    int error;

    if (error = FT_Init_FreeType(&m_ft_library))
    {
        throw "ERROR::FREETYPE: Could not init FreeType Library";
    }

    if (error = FT_New_Face(m_ft_library, font_file, 0, &m_ft_face))
    {
        throw "ERROR::FREETYPE: Failed to load font";
    }

    FT_Set_Pixel_Sizes(m_ft_face, 0, size);

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
}

FontManager::~FontManager() {
    FT_Done_Face(m_ft_face);
    FT_Done_FreeType(m_ft_library);

    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
}

const FontManager::Character & FontManager::get_character_struct(char glyph) {
    if (m_characters.find(glyph) == m_characters.end()) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (FT_Load_Char(m_ft_face, glyph, FT_LOAD_RENDER))
            throw "ERROR::FREETYPE: Failed to load Glyph";
        unsigned int texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 
            0, 
            GL_RED, 
            m_ft_face->glyph->bitmap.width, 
            m_ft_face->glyph->bitmap.rows,
            0, 
            GL_RED, 
            GL_UNSIGNED_BYTE, 
            m_ft_face->glyph->bitmap.buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        Character character = {
            texture,
            glm::ivec2(m_ft_face->glyph->bitmap.width, m_ft_face->glyph->bitmap.rows),
            glm::ivec2(m_ft_face->glyph->bitmap_left, m_ft_face->glyph->bitmap_top),
            static_cast<unsigned int>(m_ft_face->glyph->advance.x) };
        m_characters.insert(std::pair<char, Character>(glyph, character));
        glPixelStorei(GL_UNPACK_ALIGNMENT, 0);
    }

    return (m_characters.find(glyph))->second;
}

void FontManager::render() {
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_shader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VAO);

    for (auto string_and_properties : m_strings) {
        render_string(string_and_properties);
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FontManager::render_string(const StringAndProperties& sap) {
    const EnvConfig* env_config = EnvConfig::instance;
    
    glUniform3f(glGetUniformLocation(m_shader.ID, "textColor"), sap.color.r, sap.color.g, sap.color.b);

    glm::vec4 xformed_pos = env_config->screen_to_pixel_space_projection() * glm::vec4(sap.pos, 0.0f, 1.0f);
    float x = xformed_pos.x - sap.dropshadow;
    float y = xformed_pos.y + sap.dropshadow;

    float v_offset = 0.0;
    if (sap.v_align == StringAndProperties::V_ALIGN::V_CENTER)
        v_offset = -sap.scale * m_size / 2.0;
    if (sap.v_align == StringAndProperties::V_ALIGN::V_TOP)
        v_offset = -sap.scale * m_size;

    float h_offset = 0.0;
    if (sap.h_align == StringAndProperties::H_ALIGN::H_CENTER)
        h_offset = -get_string_width(sap.str) * sap.scale * 0.5;
    if (sap.h_align == StringAndProperties::H_ALIGN::H_RIGHT)
        h_offset = -get_string_width(sap.str) * sap.scale;

    std::string::const_iterator c;
    for (c = sap.str.begin(); c != sap.str.end(); c++) {
        FontManager::Character ch = get_character_struct(*c);

        float xpos = x + ch.Bearing.x * sap.scale;
        float ypos = y - (ch.Size.y - ch.Bearing.y) * sap.scale;

        float w = ch.Size.x * sap.scale;
        float h = ch.Size.y * sap.scale;
        float vertices[6][4] = {
            { xpos + h_offset,     ypos + v_offset + h,   0.0f, 0.0f },
            { xpos + h_offset,     ypos + v_offset,       0.0f, 1.0f },
            { xpos + h_offset + w, ypos + v_offset,       1.0f, 1.0f },

            { xpos + h_offset,     ypos + v_offset + h,   0.0f, 0.0f },
            { xpos + h_offset + w, ypos + v_offset,       1.0f, 1.0f },
            { xpos + h_offset + w, ypos + v_offset + h,   1.0f, 0.0f }
        };
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        x += (ch.Advance >> 6) * sap.scale;
    }
}

float FontManager::get_string_width(const std::string& str) {
    std::string::const_iterator c;
    float width = 0.0;
    for (c = str.begin(); c != str.end(); c++) {
        FontManager::Character ch = get_character_struct(*c);
        width += (ch.Advance >> 6);
    }
    return width;
}

void FontManager::add_string(const StringAndProperties& sap) {
    if (sap.dropshadow > 0.0) {
        StringAndProperties sap_copy = sap;
        sap_copy.dropshadow = 0.0;
        sap_copy.color = glm::vec3(0.0, 0.0, 0.0);
        m_strings.push_back(sap_copy);
    }

    m_strings.push_back(sap);
}


void FontManager::update_time(uint64_t timestamp) {
    m_time = timestamp;
    purge_expired_strings();
}

void FontManager::purge_expired_strings() {
    auto sap = m_strings.begin();
    while (sap != m_strings.end()) {
        bool erase = false;
        int ttl = sap->ttl;

        if (ttl == 0 || (ttl > 0 && (unsigned long long)ttl < m_time)) {
            erase = true;
        }

        // You can't erase the current iterator location. You have to move to the next
        // bucket, THEN erase the previous one.
        auto prev = sap;
        ++sap;
        if (erase) {
            m_strings.erase(prev);
        }
    }
}