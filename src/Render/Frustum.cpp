// ---------------------------------------------------------------------------
// Frustum.cpp
// ---------------------------------------------------------------------------
#include "Render/Frustum.h"

void Frustum::update(const glm::mat4& m) {
    // Rows of the matrix (GLM is column-major: m[col][row]).
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);

    m_planes[0] = r3 + r0; // left
    m_planes[1] = r3 - r0; // right
    m_planes[2] = r3 + r1; // bottom
    m_planes[3] = r3 - r1; // top
    m_planes[4] = r3 + r2; // near
    m_planes[5] = r3 - r2; // far

    for (glm::vec4& p : m_planes) {
        p /= glm::length(glm::vec3(p));
    }
}

bool Frustum::isVisible(const AABB& box) const {
    for (const glm::vec4& p : m_planes) {
        // "Positive vertex": the box corner farthest along the plane normal.
        const glm::vec3 pv(p.x >= 0.0f ? box.max.x : box.min.x,
                           p.y >= 0.0f ? box.max.y : box.min.y,
                           p.z >= 0.0f ? box.max.z : box.min.z);
        if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) return false;
    }
    return true;
}
