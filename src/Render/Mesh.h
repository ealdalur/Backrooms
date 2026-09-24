#pragma once
// ---------------------------------------------------------------------------
// Mesh.h
// CPU-side indexed geometry (MeshData) and its GPU counterpart (GpuMesh), an
// RAII owner of a VAO + VBO + EBO with optional per-instance model matrices.
//
// Vertex attribute layout shared by every mesh (matches the world shader):
//   location 0 : vec3  position
//   location 1 : vec3  normal
//   location 2 : vec2  uv
//   location 3 : vec2  (material layer, chunk-local light index or -1)
//   location 4-7 : mat4 instance model matrix (instanced meshes only)
// ---------------------------------------------------------------------------

#include "glad.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    float     material;   ///< MaterialId as float (texture array layer).
    float     lightIndex; ///< Chunk-local fixture index for emissive panels, -1 otherwise.
};

/// Indexed triangle list assembled on the CPU.
struct MeshData {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    void clear() {
        vertices.clear();
        indices.clear();
    }
    bool empty() const { return indices.empty(); }

    /// Appends another mesh, re-basing its indices.
    void append(const MeshData& other);
};

/// GPU mesh: owns a VAO/VBO/EBO and, when instanced, a streamed instance VBO.
class GpuMesh {
public:
    /// First attribute location used by the per-instance mat4.
    static constexpr GLuint kInstanceAttribute = 4;

    GpuMesh() = default;
    ~GpuMesh();
    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&& other) noexcept;
    GpuMesh& operator=(GpuMesh&& other) noexcept;

    /// Uploads geometry. When `instanced` is true an instance buffer is
    /// created and wired to attributes 4..7 with a divisor of 1.
    void upload(const MeshData& data, bool instanced = false);

    /// Streams per-instance model matrices (buffer orphaning each call).
    void setInstances(const std::vector<glm::mat4>& models);

    /// Issues an indexed draw of the whole mesh.
    void draw() const;

    /// Issues an instanced indexed draw using the last setInstances() data.
    void drawInstanced() const;

    bool valid() const { return m_vao != 0; }
    GLsizei indexCount() const { return m_indexCount; }
    GLsizei instanceCount() const { return m_instanceCount; }

private:
    void release();

    GLuint  m_vao = 0;
    GLuint  m_vbo = 0;
    GLuint  m_ebo = 0;
    GLuint  m_instanceVbo = 0;
    GLsizei m_indexCount = 0;
    GLsizei m_instanceCount = 0;
    size_t  m_instanceCapacity = 0;
};
