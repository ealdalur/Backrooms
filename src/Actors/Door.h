#pragma once
// ---------------------------------------------------------------------------
// Door.h
// An interactable wood-laminate office door hanging in a metal frame.
// The door swings (smoothly eased) about a vertical hinge axis, always away
// from the player who opened it, and exposes an AABB for collision.
// ---------------------------------------------------------------------------

#include "Physics/AABB.h"
#include "Render/Mesh.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Door {
public:
    enum class State : uint8_t { Closed, Opening, Open, Closing };

    /// Event bits reported by takeEvents() (consumed by the soundscape).
    static constexpr uint8_t kEventUnlatch = 1 << 0; ///< Opening from fully closed: lever + latch.
    static constexpr uint8_t kEventSwing   = 1 << 1; ///< Started moving (open or close, incl. reversals).
    static constexpr uint8_t kEventShut    = 1 << 2; ///< Reached closed: panel thud + latch.

    /// @param id        Globally unique, deterministic identifier (edge hash).
    /// @param hinge     World position of the hinge axis at floor level.
    /// @param closedDir Unit horizontal direction from hinge to latch when closed.
    Door(uint64_t id, const glm::vec3& hinge, const glm::vec3& closedDir);

    /// Builds the shared door geometry (panel, lever handles, hinges, kick
    /// plates) in door-local space: hinge at the origin, panel along +X.
    static MeshData buildMesh();

    /// Starts opening (away from `playerPos`) or closing; reverses mid-swing.
    void toggle(const glm::vec3& playerPos);

    /// Advances the swing animation. The door only pauses if its panel would
    /// actually intersect the player at the next step (it never pushes into
    /// them), so a player standing close to a door that swings away never
    /// blocks it.
    void update(float dt, const AABB& playerBox);

    /// Rigid model matrix for the current swing angle.
    glm::mat4 modelMatrix() const;

    /// World AABB enclosing the whole panel at its current angle.
    AABB bounds() const;

    /// Appends collision boxes that tightly follow the panel: a single box
    /// when it is axis-aligned (closed / fully open), or a chain of small
    /// boxes along the diagonal panel mid-swing.
    void appendColliders(std::vector<AABB>& out) const;

    /// World-space centre of the panel at its current angle.
    glm::vec3 center() const;

    /// Centre of the doorway at handle height (where the latch and hinges
    /// make their sounds), independent of the current swing angle.
    glm::vec3 doorwayCenter() const;

    uint64_t id() const { return m_id; }
    State state() const;

    /// Persistable state: 0 = closed, +1 / -1 = open towards that side.
    int persistentState() const;
    /// Restores a persisted state instantly (no animation, no events).
    void restoreState(int side);

    /// Returns and clears the event bits raised since the last call.
    uint8_t takeEvents() {
        const uint8_t e = m_events;
        m_events = 0;
        return e;
    }

private:
    float angleAt(float progress) const;
    float angle() const { return angleAt(m_progress); }
    glm::mat4 modelFor(float angleRadians) const;
    /// Writes the panel's collision boxes at `angleRadians` into `out`
    /// (capacity kMaxColliders) and returns how many were written. The same
    /// boxes are used for player collision and for the door's own "would I
    /// hit the player?" test, so the two can never disagree.
    int collidersAt(float angleRadians, AABB* out) const;

    static constexpr int kMaxColliders = 8;

    uint64_t  m_id;
    glm::vec3 m_hinge;
    glm::vec3 m_axis;     ///< Closed direction (hinge -> latch).
    glm::vec3 m_normal;   ///< m_axis x up: positive swing side.
    float     m_progress = 0.0f; ///< 0 = closed, 1 = fully open.
    int       m_side     = 0;    ///< Swing side (+1 / -1), 0 when closed.
    bool      m_opening  = false;
    uint8_t   m_events   = 0;    ///< Pending kEvent* bits.
};

/// A door event with the world position it happened at.
struct DoorEvent {
    uint8_t   flags;
    glm::vec3 position;
};
