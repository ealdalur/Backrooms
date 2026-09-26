#pragma once
// ---------------------------------------------------------------------------
// Physics.h
// Discrete AABB collision handling for the kinematic player body.
//
// The body is an upright box described by its feet position (bottom centre),
// a half width and a (dynamically varying) height. Movement is resolved one
// axis at a time with sub-stepping, which gives robust "slide along walls"
// behaviour, automatic step-up onto low ledges, mid-air mantling onto
// ledges just within reach, landing on top of furniture and head bumps
// against headers and the ceiling.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"

#include <glm/glm.hpp>
#include <vector>

/// Anything that can report solid obstacles inside a region of space.
class ICollisionWorld {
public:
    virtual ~ICollisionWorld() = default;
    /// Appends every solid AABB overlapping (or touching) `region` to `out`.
    virtual void gatherColliders(const AABB& region, std::vector<AABB>& out) const = 0;
};

/// Physical extents of an upright kinematic body.
struct BodyShape {
    float halfWidth = 0.3f;
    float height    = 1.8f;
};

/// Summary of what happened during a move.
struct MoveResult {
    bool  grounded   = false; ///< Body is supported from below after the move.
    bool  hitCeiling = false; ///< Upward motion was stopped.
    bool  blockedX   = false; ///< Horizontal X motion was stopped by an obstacle.
    bool  blockedZ   = false; ///< Horizontal Z motion was stopped by an obstacle.
    float steppedUp  = 0.0f;  ///< Net vertical offset applied by step-up / mantle / ground snap.
};

class Physics {
public:
    /// @param floorY    Height of the infinite floor plane.
    /// @param ceilingY  Height of the infinite ceiling plane.
    /// @param stepHeight Maximum ledge height the body climbs automatically.
    Physics(float floorY, float ceilingY, float stepHeight);

    /// World-space AABB occupied by a body standing at `feet`.
    static AABB bodyBox(const glm::vec3& feet, const BodyShape& shape);

    /// Moves the body by `displacement`, resolving collisions against `world`.
    /// @param climbHeight   When blocked horizontally, climb onto an obstacle
    ///                      whose top is at most this far above the feet (0 = never).
    ///                      A small step when grounded; a larger reach for mid-air mantling.
    /// @param snapToGround  Keep contact with the ground when walking down small ledges.
    MoveResult move(glm::vec3& feet, const BodyShape& shape, const glm::vec3& displacement,
                    float climbHeight, bool snapToGround, const ICollisionWorld& world) const;

    /// True if `box` does not overlap any obstacle, the floor or the ceiling.
    bool isFree(const AABB& box, const ICollisionWorld& world) const;

    float floorY() const { return m_floorY; }
    float ceilingY() const { return m_ceilingY; }

private:
    /// Moves along a single axis (0 = x, 1 = y, 2 = z) and clamps against the
    /// first obstacle entered. Returns true if the motion was obstructed.
    bool sweepAxis(glm::vec3& feet, const BodyShape& shape, int axis, float delta,
                   const std::vector<AABB>& colliders) const;

    float m_floorY;
    float m_ceilingY;
    float m_stepHeight;
    mutable std::vector<AABB> m_scratch; ///< Reused collider buffer (avoids per-frame allocs).
};
