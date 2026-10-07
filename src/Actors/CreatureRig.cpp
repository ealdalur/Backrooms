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

/// Squared distance between segments p1-q1 and p2-q2, with the parameters of
/// the closest points on each (Ericson, "Real-Time Collision Detection" 5.1.9).
float segmentDistance2(const glm::vec3& p1, const glm::vec3& q1, const glm::vec3& p2, const glm::vec3& q2, float& s, float& t) {
    const glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);
    constexpr float eps = 1e-8f;
    if (a <= eps && e <= eps) {
        s = t = 0.0f;
    } else if (a <= eps) {
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= eps) {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2), denom = a * e - b * b;
            s = denom > eps ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = std::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = std::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    const glm::vec3 c1 = p1 + d1 * s, c2 = p2 + d2 * t;
    return glm::dot(c1 - c2, c1 - c2);
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
