
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "font_manager.h"

FontManager::FontManager(const char * font_file, int size, int screen_width, int screen_height) :
    m_shader("text.vert", "text.frag"),
    m_strings(),
    m_time(0)
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

    for (auto i : m_strings) {
        render_string(std::get<FM_TUPLE_STR>(i), std::get<FM_TUPLE_SCALE>(i), std::get<FM_TUPLE_POS>(i), std::get<FM_TUPLE_COL>(i));
    }

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void FontManager::render_string(const std::string & str, float scale, const glm::vec2 & pos, const glm::vec3 & color) {
    glUniform3f(glGetUniformLocation(m_shader.ID, "textColor"), color.r, color.g, color.b);

    float x = pos.x;
    float y = pos.y;

    std::string::const_iterator c;
    for (c = str.begin(); c != str.end(); c++) {
        FontManager::Character ch = get_character_struct(*c);

        float xpos = x + ch.Bearing.x * scale;
        float ypos = y - (ch.Size.y - ch.Bearing.y) * scale;

        float w = ch.Size.x * scale;
        float h = ch.Size.y * scale;
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },

            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f }
        };
        glBindTexture(GL_TEXTURE_2D, ch.TextureID);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        x += (ch.Advance >> 6) * scale;
    }
}

// TODO: non-const, ownership-transfer version.
void FontManager::add_string(const std::string& str, int ttl, float scale, const glm::vec2& pos, const glm::vec3& color, float dropshadow)  {
    if (dropshadow > 0.0) {
        add_string(str, ttl, scale, pos + glm::vec2(dropshadow, -dropshadow), glm::vec3(0.0, 0.0, 0.0), 0.0);
    }

    auto str_copy = str;
    auto pos_copy = pos;
    auto color_copy = color;
    t_string queue_obj(str_copy, ttl, scale, pos_copy, color_copy);
    m_strings.push_back(queue_obj);
}

void FontManager::format(int ttl, float scale, const glm::vec2& pos, const glm::vec3& color, float dropshadow, const char* fmt, ...) {
    const int buflen = 1024 * 16;
    char buf[buflen];
    va_list argptr;
    va_start(argptr, fmt);
    size_t size = vsprintf(buf, fmt, argptr);
    va_end(argptr);
    if (size >= buflen) {
        sprintf(buf, "formatted string too long for buffer. (size = %lld)", size);
        throw buf;
    }
    add_string(buf, ttl, scale, pos, color, dropshadow);
}


void FontManager::update_time(uint64_t timestamp) {
    m_time = timestamp;
    purge_expired_strings();
}

void FontManager::purge_expired_strings() {
    auto i = m_strings.begin();
    while (i != m_strings.end()) {
        bool erase = false;
        int ttl = std::get<FM_TUPLE_TTL>(*i);

        if (ttl == 0 || (ttl > 0 && (unsigned long long)ttl < m_time)) {
            erase = true;
        }

        // You can't erase the current iterator location. You have to move to the next
        // bucket, THEN erase the previous one.
        auto prev = i;
        ++i;
        if (erase) {
            m_strings.erase(prev);
        }
    }
}