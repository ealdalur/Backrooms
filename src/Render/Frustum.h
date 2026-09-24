#pragma once
// ---------------------------------------------------------------------------
// Frustum.h
// View-frustum planes extracted from a view-projection matrix
// (Gribb/Hartmann) with a conservative AABB visibility test.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"

#include <glm/glm.hpp>
#include <array>

class Frustum {
public:
    /// Rebuilds the six planes from a combined projection * view matrix.
    void update(const glm::mat4& viewProjection);

    /// False only if the box is entirely outside at least one plane.
    bool isVisible(const AABB& box) const;

private:
    std::array<glm::vec4, 6> m_planes{}; ///< (normal.xyz, d), normals point inward.
};
