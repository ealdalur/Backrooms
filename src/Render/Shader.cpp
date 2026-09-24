// ---------------------------------------------------------------------------
// Shader.cpp
// ---------------------------------------------------------------------------
#include "Render/Shader.h"

#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>

Shader::~Shader() {
    if (m_program) glDeleteProgram(m_program);
}

GLuint Shader::compileStage(GLenum stage, const char* source, const char* debugName) {
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(shader, len, nullptr, log.data());
        std::cerr << "[Shader] " << debugName << ' '
                  << (stage == GL_VERTEX_SHADER ? "vertex" : "fragment")
                  << " stage failed to compile:\n" << log.data() << '\n';
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::build(const char* vertexSource, const char* fragmentSource, const char* debugName) {
    m_name = debugName;
    const GLuint vs = compileStage(GL_VERTEX_SHADER, vertexSource, debugName);
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, fragmentSource, debugName);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len > 1 ? len : 1), '\0');
        glGetProgramInfoLog(program, len, nullptr, log.data());
        std::cerr << "[Shader] " << debugName << " failed to link:\n" << log.data() << '\n';
        glDeleteProgram(program);
        return false;
    }

    if (m_program) glDeleteProgram(m_program);
    m_program = program;
    m_locations.clear();
    return true;
}

GLint Shader::location(const char* name) const {
    auto it = m_locations.find(name);
    if (it != m_locations.end()) return it->second;
    const GLint loc = glGetUniformLocation(m_program, name);
    m_locations.emplace(name, loc);
    return loc;
}

void Shader::set(const char* name, int v) const { glUniform1i(location(name), v); }
void Shader::set(const char* name, float v) const { glUniform1f(location(name), v); }
void Shader::set(const char* name, const glm::vec2& v) const { glUniform2fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::vec3& v) const { glUniform3fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::vec4& v) const { glUniform4fv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::ivec2& v) const { glUniform2iv(location(name), 1, glm::value_ptr(v)); }
void Shader::set(const char* name, const glm::mat4& v) const {
    glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(v));
}
void Shader::setArray(const char* name, const glm::vec4* values, int count) const {
    glUniform4fv(location(name), count, glm::value_ptr(values[0]));
}
