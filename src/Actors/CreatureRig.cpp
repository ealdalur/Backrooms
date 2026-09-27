// ---------------------------------------------------------------------------
// CreatureRig.cpp
// ---------------------------------------------------------------------------
#include "Actors/CreatureRig.h"

#include "Render/MeshBuilder.h"

#include <algorithm>
#include <cmath>

namespace rig {

TwoBone solveTwoBone(const glm::vec3& root, const glm::vec3& target, float l1, float l2, const glm::vec3& pole) {
    glm::vec3 d = target - root;
    float dist = glm::length(d);
    const glm::vec3 dir = dist > 1e-5f ? d / dist : glm::vec3(0.0f, -1.0f, 0.0f);
    dist = std::clamp(dist, std::fabs(l1 - l2) + 1e-3f, l1 + l2 - 1e-3f);

    // Law of cosines: distance along `dir` to the joint's foot point, and height.
    const float a = (l1 * l1 - l2 * l2 + dist * dist) / (2.0f * dist);
    const float h = std::sqrt(std::max(0.0f, l1 * l1 - a * a));
    glm::vec3 bend = pole - dir * glm::dot(pole, dir);
    const float bendLen = glm::length(bend);
    bend = bendLen > 1e-5f ? bend / bendLen : glm::vec3(0.0f, 0.0f, 1.0f);

    TwoBone out;
    out.joint = root + dir * a + bend * h;
    out.end = out.joint + glm::normalize(root + dir * dist - out.joint) * l2;
    return out;
}

} // namespace rig

void CreatureRig::chain(const glm::vec3* points, int count, float r0, float r1) {
    for (int i = 0; i + 1 < count; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(count - 1);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(count - 1);
        limb(points[i], points[i + 1], r0 + (r1 - r0) * t0, r0 + (r1 - r0) * t1);
    }
}

AABB CreatureRig::bounds() const {
    AABB b{glm::vec3(1e30f), glm::vec3(-1e30f)};
    for (const Limb& l : m_limbs) {
        b.merge(l.a - glm::vec3(l.ra));
        b.merge(l.a + glm::vec3(l.ra));
        b.merge(l.b - glm::vec3(l.rb));
        b.merge(l.b + glm::vec3(l.rb));
    }
    return b;
}

void CreatureRig::appendMesh(MeshData& mesh, MaterialId material, int sides) const {
    for (const Limb& l : m_limbs) mesh::addLimb(mesh, l.a, l.b, l.ra, l.rb, material, sides);
}
