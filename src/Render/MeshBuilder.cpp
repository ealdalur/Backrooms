// ---------------------------------------------------------------------------
// MeshBuilder.cpp
// ---------------------------------------------------------------------------
#include "Render/MeshBuilder.h"

#include <cmath>

namespace mesh {
namespace {

/// Tangent frame of a box face: (u, v) span the face and satisfy u x v = n,
/// which guarantees CCW winding for the corner order (-u-v, +u-v, +u+v, -u+v).
struct FaceFrame {
    Face      bit;
    glm::vec3 n;
    glm::vec3 u;
    glm::vec3 v;
};

const FaceFrame kFaces[6] = {
    {FacePosX, { 1, 0, 0}, { 0, 0, -1}, {0, 1,  0}},
    {FaceNegX, {-1, 0, 0}, { 0, 0,  1}, {0, 1,  0}},
    {FacePosY, { 0, 1, 0}, { 1, 0,  0}, {0, 0, -1}},
    {FaceNegY, { 0,-1, 0}, { 1, 0,  0}, {0, 0,  1}},
    {FacePosZ, { 0, 0, 1}, { 1, 0,  0}, {0, 1,  0}},
    {FaceNegZ, { 0, 0,-1}, {-1, 0,  0}, {0, 1,  0}},
};

inline void pushQuadIndices(MeshData& mesh, uint32_t base) {
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

} // namespace

void addQuad(MeshData& mesh, const glm::vec3 corners[4], const glm::vec3& normal,
             const glm::vec2 uvs[4], MaterialId material, float lightIndex) {
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    const float mat = static_cast<float>(material);
    for (int i = 0; i < 4; ++i) {
        mesh.vertices.push_back({corners[i], normal, uvs[i], mat, lightIndex});
    }
    pushQuadIndices(mesh, base);
}

void addBox(MeshData& mesh, const BoxDesc& d) {
    const float tile = d.tileSize > 0.0f ? d.tileSize : materialInfo(d.material).tileSize;
    const float invTile = 1.0f / tile;
    const glm::vec3 center = (d.min + d.max) * 0.5f;
    const glm::vec3 half   = (d.max - d.min) * 0.5f;
    const glm::mat3 normalMat(d.transform); // rigid transform -> no inverse-transpose needed

    static const glm::vec2 kSigns[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    for (const FaceFrame& f : kFaces) {
        if (!(d.faces & f.bit)) continue;

        // Half extents of the box measured along this face's tangent axes.
        const float hu = std::fabs(glm::dot(half, f.u));
        const float hv = std::fabs(glm::dot(half, f.v));
        const float hn = std::fabs(glm::dot(half, f.n));
        const glm::vec3 faceCenter = center + f.n * hn;

        glm::vec3 corners[4];
        glm::vec2 uvs[4];
        for (int i = 0; i < 4; ++i) {
            const glm::vec3 local = faceCenter + f.u * (kSigns[i].x * hu) + f.v * (kSigns[i].y * hv);
            const glm::vec3 rel   = local - d.uvOrigin;
            glm::vec2 uv(glm::dot(rel, f.u) * invTile, glm::dot(rel, f.v) * invTile);
            if (d.swapUV) uv = glm::vec2(uv.y, uv.x);
            corners[i] = glm::vec3(d.transform * glm::vec4(local, 1.0f));
            uvs[i]     = uv;
        }
        addQuad(mesh, corners, glm::normalize(normalMat * f.n), uvs, d.material, d.lightIndex);
    }
}

void addCylinder(MeshData& mesh, const glm::mat4& transform, float radius, float y0, float y1,
                 int segments, MaterialId material, bool caps) {
    const float invTile = 1.0f / materialInfo(material).tileSize;
    const float mat = static_cast<float>(material);
    const glm::mat3 normalMat(transform);
    const float twoPi = 6.28318530718f;

    // Side wall: segments+1 columns so the UV seam is not shared.
    const uint32_t sideBase = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        const float a = twoPi * static_cast<float>(i) / static_cast<float>(segments);
        const glm::vec3 dir(std::cos(a), 0.0f, -std::sin(a)); // CCW seen from +Y
        const glm::vec3 n = glm::normalize(normalMat * dir);
        const float u = a * radius * invTile;
        for (int k = 0; k < 2; ++k) {
            const float y = k == 0 ? y0 : y1;
            const glm::vec3 p = glm::vec3(transform * glm::vec4(dir * radius + glm::vec3(0, y, 0), 1.0f));
            mesh.vertices.push_back({p, n, glm::vec2(u, y * invTile), mat, -1.0f});
        }
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t a = sideBase + static_cast<uint32_t>(i) * 2; // bottom of column i
        const uint32_t b = a + 2;                                   // bottom of column i+1
        mesh.indices.insert(mesh.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
    }

    if (!caps) return;
    for (int k = 0; k < 2; ++k) {
        const bool top = k == 1;
        const float y = top ? y1 : y0;
        const glm::vec3 n = glm::normalize(normalMat * glm::vec3(0, top ? 1.0f : -1.0f, 0));
        const uint32_t centerIdx = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({glm::vec3(transform * glm::vec4(0, y, 0, 1)), n, glm::vec2(0.0f), mat, -1.0f});
        for (int i = 0; i <= segments; ++i) {
            const float a = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            const glm::vec3 local(std::cos(a) * radius, y, -std::sin(a) * radius);
            mesh.vertices.push_back({glm::vec3(transform * glm::vec4(local, 1.0f)), n,
                                     glm::vec2(local.x, local.z) * invTile, mat, -1.0f});
        }
        for (int i = 0; i < segments; ++i) {
            const uint32_t a = centerIdx + 1 + static_cast<uint32_t>(i);
            if (top) mesh.indices.insert(mesh.indices.end(), {centerIdx, a, a + 1});
            else     mesh.indices.insert(mesh.indices.end(), {centerIdx, a + 1, a});
        }
    }
}

} // namespace mesh
