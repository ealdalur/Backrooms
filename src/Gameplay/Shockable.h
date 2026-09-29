#pragma once
// ---------------------------------------------------------------------------
// Shockable.h
// What the Tesla gun needs to know about the things it can hit, without
// knowing what they are (the EntityDirector implements it for the Wanderer
// and the Stalker): where a discharge would jump to, whether a piece of a
// bolt actually passes through a body, and the damage a connected arc does.
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>

class IShockable {
public:
    virtual ~IShockable() = default;

    /// The body point an arc from `from` would jump to: within `reach`,
    /// inside the cone of half-angle acos(`cosCone`) around the unit `aim`,
    /// and in plain sight of `from`. Returns a target id (>= 0) or -1.
    virtual int arcTarget(const glm::vec3& from, const glm::vec3& aim, float reach, float cosCone, glm::vec3& point) const = 0;

    /// Collision of a bolt segment a-b (of half-thickness `radius`) with the
    /// bodies. Returns the id of the body it passes through, or -1, and the
    /// point of contact.
    virtual int shockTest(const glm::vec3& a, const glm::vec3& b, float radius, glm::vec3& hit) const = 0;

    /// A connected arc: `damage` (fraction of full health) delivered at `at`.
    virtual void applyShock(int target, float damage, const glm::vec3& at) = 0;
};
