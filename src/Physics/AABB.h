#pragma once
// ---------------------------------------------------------------------------
// AABB.h
// Axis-aligned bounding box used for collision, culling and interaction.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>
#include <algorithm>

struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    AABB() = default;
    AABB(const glm::vec3& mn, const glm::vec3& mx) : min(mn), max(mx) {}

    /// Builds a box from its centre and half extents.
    static AABB fromCenterHalf(const glm::vec3& c, const glm::vec3& h) { return {c - h, c + h}; }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }

    /// Strict overlap test (touching faces do NOT count as intersection),
    /// which lets a body rest exactly on top of / against a collider.
    bool intersects(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x &&
               min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }

    bool contains(const glm::vec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    AABB expanded(float amount) const { return {min - glm::vec3(amount), max + glm::vec3(amount)}; }
    AABB expanded(const glm::vec3& amount) const { return {min - amount, max + amount}; }
    AABB translated(const glm::vec3& d) const { return {min + d, max + d}; }

    /// Grows this box to include another.
    void merge(const AABB& o) {
        min = glm::min(min, o.min);
        max = glm::max(max, o.max);
    }

    /// Grows this box to include a point.
    void merge(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    /// Returns the axis-aligned bounds of this box after an affine transform.
    AABB transformed(const glm::mat4& m) const {
        AABB out{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 corner((i & 1) ? max.x : min.x,
                                   (i & 2) ? max.y : min.y,
                                   (i & 4) ? max.z : min.z);
            out.merge(glm::vec3(m * glm::vec4(corner, 1.0f)));
        }
        return out;
    }
};
