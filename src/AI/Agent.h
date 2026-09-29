#pragma once
// ---------------------------------------------------------------------------
// Agent.h
// Shared machinery of the entities: an upright kinematic body moved by the
// same collision code as the player (so they step, slide along walls and
// stand on the floor slabs), plus path following over the NavGrid with
// string pulling (waypoints that are directly visible are skipped, so paths
// cut corners naturally instead of zig-zagging through cell centres) and
// stuck detection.
//
// Also their vitals under the Tesla gun: health, the convulsions of a body
// with current running through it, slow recovery once the arc lets go, and
// death - the body vaporises over a few seconds (the renderer dissolves it
// with glowing edges and it sheds embers), after which the entity is gone
// for good.
// ---------------------------------------------------------------------------

#include "AI/NavGrid.h"
#include "Actors/CreatureRig.h"
#include "Physics/Physics.h"

#include <glm/glm.hpp>
#include <vector>

struct EntityDrawList;

class Agent {
public:
    virtual ~Agent() = default;

    bool active() const { return m_active; }
    const glm::vec3& feet() const { return m_feet; }
    int level() const { return m_level; }
    float yaw() const { return m_yaw; }
    float height() const { return m_height; }
    glm::vec3 forward() const;

    /// World AABB of the body.
    AABB bodyBox() const;

    /// Appears at a position (spawn / relocation).
    virtual void place(const glm::vec3& feet, int level, float yaw);
    /// Leaves the world until placed again (a body that is vaporising is finished off).
    virtual void deactivate() {
        vaporizeNow();
        m_active = false;
    }

    /// Seconds spent barely moving while trying to.
    float stuckTime() const { return m_stuck; }

    /// The posed body (world space): what the gun's arcs collide with.
    virtual const CreatureRig& body() const = 0;

    // ---- Vitals -------------------------------------------------------------------
    /// 1 = unhurt, 0 = dead.
    float health() const { return m_health; }
    /// Current running through it right now: 0..1 (it convulses and cannot act).
    float shock() const { return m_shock; }
    bool shocked() const { return m_shock > 0.05f; }
    /// Dead and vaporising: 0..1 progress, < 0 while alive.
    float dissolve() const { return m_dissolve; }
    bool dying() const { return m_dissolve >= 0.0f; }
    /// Vaporised completely: it will never be placed again.
    bool destroyed() const { return m_destroyed; }
    /// A connected arc delivers `damage` (fraction of full health).
    void applyShock(float damage);
    /// Ends a vaporisation at once (the world is being reset around the player).
    void vaporizeNow();

protected:
    /// What updateVitals() noticed this frame.
    struct VitalSigns {
        bool pain = false; ///< Time for a scream of pain.
        bool died = false; ///< Health just ran out: the vaporisation starts.
    };
    /// Advances shock, recovery, the dissolve and its embers. Call first thing
    /// in the entity's update; while shocked() or dying() it should only stand
    /// and convulse().
    VitalSigns updateVitals(float dt);
    /// Makes the posed body twitch and jerk with the current running through it.
    void convulse(CreatureRig& rig) const;
    /// Appends the glowing embers of a vaporising body.
    void buildEmbers(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const;

    Agent(float halfWidth, float height) : m_halfWidth(halfWidth), m_height(height) {}

    /// Plans a path to `goal`. Returns false if nothing useful was found.
    bool planTo(const NavGrid& nav, const glm::vec3& goal, const NavProfile& profile);
    void clearPath() { m_path.clear(); m_pathIndex = 0; }
    bool hasPath() const { return m_pathIndex < m_path.size(); }
    /// The waypoint currently steered at (after string pulling).
    glm::vec3 steerTarget(const NavGrid& nav);
    /// True if the current waypoint has been reached (or cleanly passed).
    bool reachedWaypoint() const;
    /// True if the whole body width could move straight to `to` without
    /// meeting a wall, a closed door or the solid part of an opening.
    bool clearRun(const NavGrid& nav, const glm::vec3& to) const;
    /// The next cell crossing still ahead on the path (for door handling).
    bool nextCrossing(glm::ivec2& from, glm::ivec2& to) const;

    /// Moves along the path at `speed`. Returns true once the goal is reached.
    bool followPath(float dt, float speed, float accel, const NavGrid& nav, const ICollisionWorld& world,
                    const Physics& physics);

    /// Accelerates towards a horizontal velocity, applies gravity and collides.
    void integrate(float dt, const glm::vec3& desiredVelocity, float accel, const ICollisionWorld& world,
                   const Physics& physics);

    /// Turns towards a horizontal direction at `rate` rad/s.
    void turnTowards(const glm::vec3& dir, float rate, float dt);

    bool      m_active = false;
    glm::vec3 m_feet{0.0f};
    glm::vec3 m_velocity{0.0f};
    int       m_level = 0;
    float     m_yaw = 0.0f;
    float     m_halfWidth;
    float     m_height;
    bool      m_grounded = false;
    float     m_stuck = 0.0f;

    std::vector<glm::vec3>  m_path;      ///< Waypoints (edge crossings, then the goal).
    std::vector<glm::ivec2> m_pathCells; ///< Cells the path runs through.
    size_t                  m_pathIndex = 0;

    // Getting round furniture (which sight lines, and so string pulling, ignore).
    float m_detour = 0.0f;        ///< Seconds left of a sidestep manoeuvre.
    float m_noShortcuts = 0.0f;   ///< Seconds left of following waypoints strictly.
    float m_detourSide = 1.0f;    ///< Alternates the sidestep direction.

    // Vitals.
    float m_health = 1.0f;
    float m_vulnerability = 1.0f; ///< Damage multiplier.
    float m_shock = 0.0f;
    float m_sinceShock = 1e3f;    ///< Seconds since an arc last connected.
    float m_painTimer = 0.0f;
    float m_dissolve = -1.0f;
    bool  m_justDied = false;
    bool  m_destroyed = false;
    float m_vitalTime = 0.0f;     ///< Clock of the convulsions.

private:
    struct Ember {
        glm::vec3 pos, vel;
        float     age, life, size;
    };
    std::vector<Ember> m_embers;
    float              m_emberTimer = 0.0f;
    uint64_t           m_emberSeed = 0x3B1Du;
};
