#pragma once
// ---------------------------------------------------------------------------
// Shader.h
// RAII wrapper around a linked GLSL program built from raw string literals,
// with compile/link diagnostics and a cached uniform location lookup.
// ---------------------------------------------------------------------------

#include "glad.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class Shader {
public:
    Shader() = default;
    ~Shader();
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    /// Compiles and links a vertex + fragment program. On failure the full
    /// driver log is written to stderr and false is returned.
    bool build(const char* vertexSource, const char* fragmentSource, const char* debugName);

    void use() const { glUseProgram(m_program); }
    GLuint id() const { return m_program; }

    /// Returns the (cached) location of a uniform, or -1 if it is inactive.
    GLint location(const char* name) const;

    void set(const char* name, int v) const;
    void set(const char* name, float v) const;
    void set(const char* name, const glm::vec2& v) const;
    void set(const char* name, const glm::vec3& v) const;
    void set(const char* name, const glm::vec4& v) const;
    void set(const char* name, const glm::ivec2& v) const;
    void set(const char* name, const glm::mat4& v) const;
    void setArray(const char* name, const glm::vec4* values, int count) const;

private:
    static GLuint compileStage(GLenum stage, const char* source, const char* debugName);

    GLuint m_program = 0;
    std::string m_name;
    mutable std::unordered_map<std::string, GLint> m_locations;
};
