// ---------------------------------------------------------------------------
// Physics.cpp
// Axis-separated, sub-stepped AABB collision resolution with step-up.
// ---------------------------------------------------------------------------
#include "Physics/Physics.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kSkin         = 1e-4f; ///< Separation kept from obstacles to avoid float re-penetration.
constexpr float kMaxSubstep   = 0.20f; ///< Max displacement per sub-step (< body width -> no tunnelling).
constexpr float kMinStepGain  = 1e-4f; ///< Minimum extra progress needed to accept a step-up.
} // namespace

Physics::Physics(float floorY, float ceilingY, float stepHeight)
    : m_floorY(floorY), m_ceilingY(ceilingY), m_stepHeight(stepHeight) {}

AABB Physics::bodyBox(const glm::vec3& feet, const BodyShape& shape) {
    return {glm::vec3(feet.x - shape.halfWidth, feet.y, feet.z - shape.halfWidth),
            glm::vec3(feet.x + shape.halfWidth, feet.y + shape.height, feet.z + shape.halfWidth)};
}

bool Physics::sweepAxis(glm::vec3& feet, const BodyShape& shape, int axis, float delta,
                        const std::vector<AABB>& colliders) const {
    if (delta == 0.0f) return false;

    const AABB start = bodyBox(feet, shape);
    feet[axis] += delta;
    const AABB moved = bodyBox(feet, shape);

    // Offsets from the feet position to the body's min / max faces on this axis.
    const float minOffset = (axis == 1) ? 0.0f : -shape.halfWidth;
    const float maxOffset = (axis == 1) ? shape.height : shape.halfWidth;

    bool hit = false;
    for (const AABB& c : colliders) {
        if (!moved.intersects(c)) continue;
        // Obstacles we already overlapped before moving are ignored so that a
        // body that ended up inside geometry (e.g. a door closing on it) can
        // always walk out instead of being snapped through it.
        if (start.intersects(c)) continue;

        if (delta > 0.0f) {
            feet[axis] = std::min(feet[axis], c.min[axis] - maxOffset - kSkin);
        } else {
            feet[axis] = std::max(feet[axis], c.max[axis] - minOffset + kSkin);
        }
        hit = true;
    }

    // The analytic floor and ceiling planes bound vertical motion everywhere.
    if (axis == 1) {
        if (feet.y < m_floorY) {
            feet.y = m_floorY;
            hit = hit || delta < 0.0f;
        }
        if (feet.y + shape.height > m_ceilingY) {
            feet.y = m_ceilingY - shape.height;
            hit = hit || delta > 0.0f;
        }
    }
    return hit;
}

MoveResult Physics::move(glm::vec3& feet, const BodyShape& shape, const glm::vec3& displacement,
                         float climbHeight, bool snapToGround, const ICollisionWorld& world) const {
    MoveResult result;

    // Gather every obstacle the body could possibly touch during this move.
    const float reach = glm::length(displacement) + std::max(m_stepHeight, climbHeight) + 0.25f;
    m_scratch.clear();
    world.gatherColliders(bodyBox(feet, shape).expanded(reach), m_scratch);

    const float largest = std::max(glm::length(glm::vec2(displacement.x, displacement.z)),
                                   std::fabs(displacement.y));
    const int   steps   = std::max(1, static_cast<int>(std::ceil(largest / kMaxSubstep)));
    const glm::vec3 d   = displacement / static_cast<float>(steps);

    for (int s = 0; s < steps; ++s) {
        // ---- Horizontal axes (X then Z) with step-up -----------------------
        for (int axis : {0, 2}) {
            if (d[axis] == 0.0f) continue;

            const glm::vec3 before = feet;
            bool hit = sweepAxis(feet, shape, axis, d[axis], m_scratch);

            if (hit && climbHeight > 0.0f) {
                // Attempt to climb: raise, move, then settle back down. Only
                // succeeds if the obstacle's top is within climbHeight and there
                // is headroom above it, so walls can never be climbed.
                glm::vec3 stepPos = before;
                const float yStart = stepPos.y;
                sweepAxis(stepPos, shape, 1, climbHeight, m_scratch);
                const float raised = stepPos.y - yStart;
                if (raised > 0.01f) {
                    const bool hitRaised = sweepAxis(stepPos, shape, axis, d[axis], m_scratch);
                    const float plainProgress = std::fabs(feet[axis] - before[axis]);
                    const float stepProgress  = std::fabs(stepPos[axis] - before[axis]);
                    if (stepProgress > plainProgress + kMinStepGain) {
                        sweepAxis(stepPos, shape, 1, -raised, m_scratch); // land on the ledge top
                        result.steppedUp += stepPos.y - before.y;
                        feet = stepPos;
                        hit  = hitRaised;
                    }
                }
            }

            if (hit) {
                if (axis == 0) result.blockedX = true;
                else           result.blockedZ = true;
            }
        }

        // ---- Vertical axis ---------------------------------------------------
        if (d.y != 0.0f) {
            if (sweepAxis(feet, shape, 1, d.y, m_scratch)) {
                if (d.y < 0.0f) result.grounded   = true;
                else            result.hitCeiling = true;
            }
        }
    }

    // ---- Ground snapping: stay glued to the floor over small drops ---------
    if (snapToGround && !result.grounded && displacement.y <= 0.0f) {
        glm::vec3 probe = feet;
        if (sweepAxis(probe, shape, 1, -m_stepHeight, m_scratch)) {
            result.steppedUp += probe.y - feet.y;
            feet = probe;
            result.grounded = true;
        }
    }
    return result;
}

bool Physics::isFree(const AABB& box, const ICollisionWorld& world) const {
    if (box.min.y < m_floorY - kSkin || box.max.y > m_ceilingY + kSkin) return false;
    m_scratch.clear();
    world.gatherColliders(box, m_scratch);
    for (const AABB& c : m_scratch) {
        if (box.intersects(c)) return false;
    }
    return true;
}
