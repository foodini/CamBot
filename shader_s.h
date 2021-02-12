#ifndef SHADER_H
#define SHADER_H

#include <iostream>
#include "stdio.h"

#include "glm/gtc/type_ptr.hpp"
#include <glad/glad.h>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#pragma warning(suppress : 4101)

#define REFRESH_FREQUENCY 30

class Shader
{
public:
    unsigned int ID;

    uint64_t get_path_mtime(const char* path) {
        struct stat file_info;
        stat(path, &file_info);
        return file_info.st_mtime;
    }

    // constructor generates the shader on the fly
    // ------------------------------------------------------------------------
    void shader_setup(const char* vertex_path, const char* fragment_path)
    {
        strcpy(m_vertex_path, vertex_path);
        strcpy(m_fragment_path, fragment_path);
        // 1. retrieve the vertex/fragment source code from filePath
        std::string vertexCode;
        std::string fragmentCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        // ensure ifstream objects can throw exceptions:
        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        try
        {
            // open files
            vShaderFile.open(vertex_path);
            fShaderFile.open(fragment_path);
            std::stringstream vShaderStream, fShaderStream;
            // read file's buffer contents into streams
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            std::cout << vShaderFile.rdbuf();
            // close file handlers
            vShaderFile.close();
            fShaderFile.close();
            // convert stream into string
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure)
        {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ" << std::endl;
        }
        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();
        // 2. compile shaders
        unsigned int vertex, fragment;
        // vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        checkCompileErrors(vertex, "VERTEX");
        m_last_vertex_update = get_path_mtime(m_vertex_path);
        // fragment Shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        checkCompileErrors(fragment, "FRAGMENT");
        m_last_fragment_update = get_path_mtime(m_fragment_path);
        // shader Program
        ID = glCreateProgram();
        glAttachShader(ID, vertex);
        glAttachShader(ID, fragment);
        glLinkProgram(ID);
        checkCompileErrors(ID, "PROGRAM");
        // delete the shaders as they're linked into our program now and no longer necessary
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    Shader(const char* vertex_path, const char* fragment_path) :
        m_refresh_countdown(REFRESH_FREQUENCY)
    {
        shader_setup(vertex_path, fragment_path);
    }

    // activate the shader
    // ------------------------------------------------------------------------
    void use()
    {
#ifdef _DEBUG
        if (m_refresh_countdown-- == 0) {
            if (get_path_mtime(m_fragment_path) != m_last_fragment_update || get_path_mtime(m_vertex_path) != m_last_vertex_update) {
                shader_setup(m_vertex_path, m_fragment_path);
            }
            m_refresh_countdown = REFRESH_FREQUENCY;
        }
#endif
        glUseProgram(ID);
    }

    // UNIFORMS MUST BE SET AFTER A use() CALL!!!!!
    void setBool(const std::string& name, bool value) const
    {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), (int)value);
    }

    // UNIFORMS MUST BE SET AFTER A use() CALL!!!!!
    void setInt(const std::string& name, int value) const
    {
        glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
    }

    // UNIFORMS MUST BE SET AFTER A use() CALL!!!!!
    void setFloat(const std::string& name, float value) const
    {
        glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
    }

    void setFloat2(const std::string& name, float value_x, float value_y) const
    {
        glUniform2f(glGetUniformLocation(ID, name.c_str()), value_x, value_y);
    }

    void setFloat2(const std::string& name, glm::vec2& v) const
    {
        glUniform2f(glGetUniformLocation(ID, name.c_str()), v.x, v.y);
    }

    void setFloat3(const std::string& name, float value_x, float value_y, float value_z) const
    {
        glUniform3f(glGetUniformLocation(ID, name.c_str()), value_x, value_y, value_z);
    }

    void setFloat3(const std::string& name, glm::vec3& v) const
    {
        glUniform3f(glGetUniformLocation(ID, name.c_str()), v.x, v.y, v.z);
    }

    void setFloat4(const std::string& name, float value_x, float value_y, float value_z, float value_w) const
    {
        glUniform4f(glGetUniformLocation(ID, name.c_str()), value_x, value_y, value_z, value_w);
    }

    void setFloat4(const std::string& name, glm::vec4& v) const
    {
        glUniform4f(glGetUniformLocation(ID, name.c_str()), v.x, v.y, v.z, v.w);
    }

private:
    uint32_t m_refresh_countdown;
    uint64_t m_last_vertex_update;
    uint64_t m_last_fragment_update;
    char     m_vertex_path[1024];
    char     m_fragment_path[1024];
    // utility function for checking shader compilation/linking errors.
    // ------------------------------------------------------------------------
    void checkCompileErrors(unsigned int shader, std::string type)
    {
        int success;
        char infoLog[1024];
        if (type != "PROGRAM")
        {
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                glGetShaderInfoLog(shader, 1024, NULL, infoLog);
                fprintf(stderr, "ERROR::SHADER_COMPILATION_ERROR of type: %s\n%s\n -- --------------------------------------------------- -- \n",
                    type.c_str(), infoLog);
                throw "Compilation Error";
            }
        }
        else
        {
            glGetProgramiv(shader, GL_LINK_STATUS, &success);
            if (!success)
            {
                glGetProgramInfoLog(shader, 1024, NULL, infoLog);
                fprintf(stderr, "ERROR::PROGRAM_LINKING_ERROR of type: %s\n%s\n -- --------------------------------------------------- -- \n",
                    type.c_str(), infoLog);
                throw "Compilation Error";
            }
        }
    }
};
#endif