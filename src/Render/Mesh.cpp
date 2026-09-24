// ---------------------------------------------------------------------------
// Mesh.cpp
// ---------------------------------------------------------------------------
#include "Render/Mesh.h"

#include <cstddef>
#include <utility>

void MeshData::append(const MeshData& other) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    indices.reserve(indices.size() + other.indices.size());
    for (uint32_t i : other.indices) indices.push_back(base + i);
}

GpuMesh::~GpuMesh() { release(); }

GpuMesh::GpuMesh(GpuMesh&& other) noexcept { *this = std::move(other); }

GpuMesh& GpuMesh::operator=(GpuMesh&& other) noexcept {
    if (this != &other) {
        release();
        m_vao              = std::exchange(other.m_vao, 0);
        m_vbo              = std::exchange(other.m_vbo, 0);
        m_ebo              = std::exchange(other.m_ebo, 0);
        m_instanceVbo      = std::exchange(other.m_instanceVbo, 0);
        m_indexCount       = std::exchange(other.m_indexCount, 0);
        m_instanceCount    = std::exchange(other.m_instanceCount, 0);
        m_instanceCapacity = std::exchange(other.m_instanceCapacity, 0);
    }
    return *this;
}

void GpuMesh::release() {
    if (m_instanceVbo) glDeleteBuffers(1, &m_instanceVbo);
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = m_instanceVbo = 0;
    m_indexCount = m_instanceCount = 0;
    m_instanceCapacity = 0;
}

void GpuMesh::upload(const MeshData& data, bool instanced) {
    release();
    if (data.empty()) return;

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.indices.size() * sizeof(uint32_t)),
                 data.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, material)));

    if (instanced) {
        glGenBuffers(1, &m_instanceVbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4), nullptr, GL_STREAM_DRAW);
        m_instanceCapacity = 1;
        // A mat4 attribute occupies four consecutive vec4 locations.
        for (GLuint c = 0; c < 4; ++c) {
            const GLuint loc = kInstanceAttribute + c;
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  reinterpret_cast<void*>(sizeof(glm::vec4) * c));
            glVertexAttribDivisor(loc, 1);
        }
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    m_indexCount = static_cast<GLsizei>(data.indices.size());
}

void GpuMesh::setInstances(const std::vector<glm::mat4>& models) {
    m_instanceCount = static_cast<GLsizei>(models.size());
    if (!m_instanceVbo || models.empty()) return;

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    const GLsizeiptr bytes = static_cast<GLsizeiptr>(models.size() * sizeof(glm::mat4));
    // Grow geometrically so the capacity settles after a few frames.
    if (models.size() > m_instanceCapacity) m_instanceCapacity = models.size() * 2;
    // (Re)specifying the storage orphans the previous block, so the driver
    // never stalls waiting for in-flight draws that still read it.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_instanceCapacity * sizeof(glm::mat4)),
                 nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, models.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GpuMesh::draw() const {
    if (!m_vao || m_indexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
}

void GpuMesh::drawInstanced() const {
    if (!m_vao || m_indexCount == 0 || m_instanceCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr, m_instanceCount);
}
