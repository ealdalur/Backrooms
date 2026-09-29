#pragma once
// ---------------------------------------------------------------------------
// CreatureRig.h
// Code-driven creature bodies. There are no rigged models: each entity poses
// a set of joints procedurally every frame (gait cycles, two-bone IK for
// arms and legs, head tracking) and the rig turns the resulting limb list
// into tapered, capsule-jointed geometry.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/MaterialTypes.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace rig {

/// Result of a two-bone IK solve.
struct TwoBone {
    glm::vec3 joint; ///< Knee / elbow.
    glm::vec3 end;   ///< Foot / hand (== target when reachable).
};

/// Solves a two-bone chain of lengths l1, l2 from `root` towards `target`,
/// bending the joint towards `pole` (a direction).
TwoBone solveTwoBone(const glm::vec3& root, const glm::vec3& target, float l1, float l2, const glm::vec3& pole);

} // namespace rig

class CreatureRig {
public:
    struct Limb {
        glm::vec3 a, b;
        float     ra, rb;
    };

    void clear() { m_limbs.clear(); }

    /// Adds one tapered segment.
    void limb(const glm::vec3& a, const glm::vec3& b, float ra, float rb) { m_limbs.push_back({a, b, ra, rb}); }

    /// Adds segments through `count` points, radii interpolated from r0 to r1.
    void chain(const glm::vec3* points, int count, float r0, float r1);

    const std::vector<Limb>& limbs() const { return m_limbs; }

    /// Moves every joint through `fn` (a function of position, so limbs that
    /// share a joint stay joined): convulsions, shudders.
    template <typename Fn>
    void warp(Fn&& fn) {
        for (Limb& l : m_limbs) {
            l.a = fn(l.a);
            l.b = fn(l.b);
        }
    }

    /// World-space bounds of every limb (incl. radii).
    AABB bounds() const;

    /// Appends the body geometry.
    void appendMesh(MeshData& mesh, MaterialId material, int sides = 10) const;

private:
    std::vector<Limb> m_limbs;
};
