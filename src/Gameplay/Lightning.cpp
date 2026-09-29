// ---------------------------------------------------------------------------
// Lightning.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/Lightning.h"

#include <algorithm>
#include <cmath>

namespace lightning {
namespace {

constexpr float kTwoPi = 6.28318530718f;

/// Two unit vectors perpendicular to the unit `d` (and to each other).
void basis(const glm::vec3& d, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 ref = std::fabs(d.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    u = glm::normalize(glm::cross(d, ref));
    v = glm::cross(d, u);
}

} // namespace

void jagged(std::vector<glm::vec3>& out, const glm::vec3& a, const glm::vec3& b, float roughness, int levels, rnd::Rng& rng) {
    out.clear();
    out.push_back(a);
    out.push_back(b);
    const glm::vec3 span = b - a;
    const float length = glm::length(span);
    if (length < 1e-4f) return;
    glm::vec3 u, v;
    basis(span / length, u, v);
    std::vector<glm::vec3> next;
    for (int level = 0; level < levels; ++level) {
        next.clear();
        next.reserve(out.size() * 2);
        for (size_t i = 0; i + 1 < out.size(); ++i) {
            const glm::vec3& p = out[i];
            const glm::vec3& q = out[i + 1];
            const float seg = glm::length(q - p);
            // Offset in the plane across the bolt: a random direction, a
            // (roughly normal) random amount - long segments kink the most.
            const float angle = rng.range(0.0f, kTwoPi);
            const float amount = (rng.nextFloat() + rng.nextFloat() - 1.0f) * roughness * seg;
            next.push_back(p);
            next.push_back((p + q) * 0.5f + (u * std::cos(angle) + v * std::sin(angle)) * amount);
        }
        next.push_back(out.back());
        out.swap(next);
    }
}

size_t strike(std::vector<Bolt>& out, const glm::vec3& a, const glm::vec3& b, const StrikeParams& params, rnd::Rng& rng) {
    Bolt bolt;
    bolt.width = params.width;
    bolt.intensity = params.intensity;
    bolt.life = params.life;
    bolt.color = params.color;
    jagged(bolt.points, a, b, params.roughness, params.levels, rng);
    const size_t index = out.size();
    out.push_back(bolt);
    if (params.forkDepth <= 0 || params.levels < 2) return index;

    // Forks leave the channel at a random angle and wander off towards
    // nothing in particular, shorter and fainter the further out they start.
    const std::vector<glm::vec3> path = out[index].points; // copy: `out` may reallocate below
    const float total = glm::length(b - a);
    for (size_t i = 2; i + 2 < path.size(); ++i) {
        if (!rng.chance(params.forkChance)) continue;
        const float along = static_cast<float>(i) / static_cast<float>(path.size() - 1);
        const glm::vec3 tangent = glm::normalize(path[i + 1] - path[i - 1] + glm::vec3(1e-5f));
        const glm::vec3 dir = coneDirection(tangent, rng.range(0.45f, 1.1f), rng);
        const float length = total * (1.0f - along) * rng.range(0.25f, 0.6f);
        if (length < 0.05f) continue;
        StrikeParams fork = params;
        fork.width *= 0.45f;
        fork.intensity *= 0.6f;
        fork.levels = std::max(2, params.levels - 1);
        fork.forkChance *= 0.5f;
        fork.forkDepth = params.forkDepth - 1;
        fork.roughness *= 1.15f;
        strike(out, path[i], path[i] + dir * length, fork, rng);
    }
    return index;
}

glm::vec3 coneDirection(const glm::vec3& axis, float maxAngle, rnd::Rng& rng) {
    glm::vec3 u, v;
    basis(axis, u, v);
    const float cosMax = std::cos(maxAngle);
    const float cosT = 1.0f - rng.nextFloat() * (1.0f - cosMax);
    const float sinT = std::sqrt(std::max(0.0f, 1.0f - cosT * cosT));
    const float phi = rng.range(0.0f, kTwoPi);
    return glm::normalize(axis * cosT + (u * std::cos(phi) + v * std::sin(phi)) * sinT);
}

} // namespace lightning
