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

void addLimb(MeshData& mesh, const glm::vec3& a, const glm::vec3& b, float ra, float rb, MaterialId material,
             int sides, int capRings) {
    glm::vec3 axis = b - a;
    const float len = glm::length(axis);
    if (len < 1e-5f) return;
    axis /= len;
    // Orthonormal frame around the axis.
    const glm::vec3 ref = std::fabs(axis.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 u = glm::normalize(glm::cross(ref, axis));
    const glm::vec3 v = glm::cross(axis, u);

    // Slope of the frustum side: normals tilt towards the thinner end.
    const float slope = std::atan2(ra - rb, len);

    // Rings from the tip of cap A to the tip of cap B. Each ring: centre on
    // the axis, radius, and the elevation angle of its normal.
    struct Ring {
        glm::vec3 centre;
        float     radius;
        float     elevation; ///< Normal angle out of the radial plane (towards +axis).
    };
    std::vector<Ring> rings;
    const float halfPi = 1.5707963f;
    for (int i = 0; i <= capRings; ++i) { // cap A: from the pole (-axis) to the equator
        const float phi = -halfPi + (halfPi + slope) * static_cast<float>(i) / static_cast<float>(capRings);
        rings.push_back({a + axis * (ra * std::sin(phi)), ra * std::cos(phi), phi});
    }
    for (int i = 0; i <= capRings; ++i) { // cap B: from its equator to the pole (+axis)
        const float phi = slope + (halfPi - slope) * static_cast<float>(i) / static_cast<float>(capRings);
        rings.push_back({b + axis * (rb * std::sin(phi)), rb * std::cos(phi), phi});
    }

    const float invTile = 1.0f / materialInfo(material).tileSize;
    const float mat = static_cast<float>(material);
    const float twoPi = 6.28318530718f;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (size_t r = 0; r < rings.size(); ++r) {
        const Ring& ring = rings[r];
        const float along = glm::dot(ring.centre - a, axis);
        for (int s = 0; s <= sides; ++s) {
            const float t = twoPi * static_cast<float>(s) / static_cast<float>(sides);
            const glm::vec3 radial = u * std::cos(t) + v * std::sin(t);
            const glm::vec3 n = radial * std::cos(ring.elevation) + axis * std::sin(ring.elevation);
            const glm::vec3 p = ring.centre + radial * ring.radius;
            const glm::vec2 uv(t * std::max(ra, rb) * invTile, along * invTile);
            mesh.vertices.push_back({p, glm::normalize(n), uv, mat, -1.0f});
        }
    }
    const uint32_t stride = static_cast<uint32_t>(sides + 1);
    for (uint32_t r = 0; r + 1 < rings.size(); ++r) {
        for (uint32_t s = 0; s < static_cast<uint32_t>(sides); ++s) {
            const uint32_t i0 = base + r * stride + s;
            const uint32_t i1 = i0 + stride;
            // CCW seen from outside: ring r -> r+1 runs along +axis, s along +angle (u -> v).
            mesh.indices.insert(mesh.indices.end(), {i0, i0 + 1, i1 + 1, i0, i1 + 1, i1});
        }
    }
}

void addTorus(MeshData& mesh, const glm::mat4& transform, float majorRadius, float minorRadius, int ringSegments,
              int tubeSegments, MaterialId material) {
    const float invTile = 1.0f / materialInfo(material).tileSize;
    const float mat = static_cast<float>(material);
    const glm::mat3 normalMat(transform);
    const float twoPi = 6.28318530718f;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i <= ringSegments; ++i) {
        const float a = twoPi * static_cast<float>(i) / static_cast<float>(ringSegments);
        const glm::vec3 radial(std::cos(a), 0.0f, -std::sin(a)); // CCW seen from +Y
        for (int j = 0; j <= tubeSegments; ++j) {
            const float b = twoPi * static_cast<float>(j) / static_cast<float>(tubeSegments);
            const glm::vec3 n = radial * std::cos(b) + glm::vec3(0.0f, std::sin(b), 0.0f);
            const glm::vec3 p = radial * majorRadius + n * minorRadius;
            mesh.vertices.push_back({glm::vec3(transform * glm::vec4(p, 1.0f)), glm::normalize(normalMat * n),
                                     glm::vec2(a * majorRadius * invTile, b * minorRadius * invTile), mat, -1.0f});
        }
    }
    const uint32_t row = static_cast<uint32_t>(tubeSegments + 1);
    for (int i = 0; i < ringSegments; ++i) {
        for (int j = 0; j < tubeSegments; ++j) {
            const uint32_t a = base + static_cast<uint32_t>(i) * row + static_cast<uint32_t>(j);
            const uint32_t b = a + row; // next ring position
            // Outward-facing, counter-clockwise: ring direction x tube direction points out.
            mesh.indices.insert(mesh.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
        }
    }
}

} // namespace mesh
