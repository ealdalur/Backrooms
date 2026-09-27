// ---------------------------------------------------------------------------
// Agent.cpp
// ---------------------------------------------------------------------------
#include "AI/Agent.h"

#include "Core/Config.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>

namespace {
// A doorway is 0.9 m clear between the jambs. Arriving within 0.25 m of a
// point 0.9 m out, a body heading for the matching point on the far side is
// at most ~0.13 m off-centre in the opening, so even the widest agent
// (0.56 m) clears both jambs.
constexpr float kArriveRadius = 0.25f;   ///< Waypoint reached within this horizontal distance.
constexpr float kDoorApproach = 0.9f;    ///< Waypoints either side of narrow openings.
constexpr float kPi = 3.14159265f;

inline glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }
} // namespace

glm::vec3 Agent::forward() const { return {std::sin(m_yaw), 0.0f, std::cos(m_yaw)}; }

AABB Agent::bodyBox() const { return Physics::bodyBox(m_feet, {m_halfWidth, m_height}); }

void Agent::place(const glm::vec3& feet, int level, float yaw) {
    m_active = true;
    m_feet = feet;
    m_level = level;
    m_yaw = yaw;
    m_velocity = glm::vec3(0.0f);
    m_grounded = false;
    m_stuck = 0.0f;
    clearPath();
}

bool Agent::planTo(const NavGrid& nav, const glm::vec3& goal, const NavProfile& profile) {
    const glm::ivec2 start = NavGrid::cellOf(m_feet);
    const glm::ivec2 goalCell = NavGrid::cellOf(goal);
    const bool reached = nav.findPath(m_level, start, goalCell, profile, m_pathCells);

    m_path.clear();
    m_pathIndex = 0;
    for (size_t i = 1; i < m_pathCells.size(); ++i) {
        const glm::ivec2 a = m_pathCells[i - 1], b = m_pathCells[i];
        // Line up with the opening before passing through it, square to the
        // edge (string pulling drops these where there is room to cut across).
        const glm::vec3 c = nav.crossing(m_level, a, b);
        const glm::vec3 through(static_cast<float>(b.x - a.x), 0.0f, static_cast<float>(b.y - a.y));
        m_path.push_back(c - through * kDoorApproach);
        m_path.push_back(c + through * kDoorApproach);
    }
    if (reached) {
        m_path.emplace_back(goal.x, world::levelFloorY(m_level), goal.z);
    } else if (m_pathCells.size() > 1) {
        m_path.push_back(NavGrid::cellCenter(m_pathCells.back(), m_level));
    }
    return !m_path.empty();
}

bool Agent::clearRun(const NavGrid& nav, const glm::vec3& to) const {
    const glm::vec2 a = xz(m_feet), b = xz(to);
    const glm::vec2 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-3f) return true;
    const glm::vec2 side = glm::vec2(-d.y, d.x) / len * (m_halfWidth * 1.3f);
    return nav.lineOfSight(m_level, a, b) && nav.lineOfSight(m_level, a + side, b + side) &&
           nav.lineOfSight(m_level, a - side, b - side);
}

glm::vec3 Agent::steerTarget(const NavGrid& nav) {
    // String pulling: skip ahead while the next waypoint is directly reachable.
    if (m_noShortcuts <= 0.0f) {
        while (m_pathIndex + 1 < m_path.size() && clearRun(nav, m_path[m_pathIndex + 1])) ++m_pathIndex;
    }
    return m_path[m_pathIndex];
}

bool Agent::reachedWaypoint() const {
    const glm::vec2 p = xz(m_feet), t = xz(m_path[m_pathIndex]);
    if (glm::length(p - t) < kArriveRadius) return true;
    if (m_pathIndex + 1 >= m_path.size()) return false;
    // Already past it and close to the line on to the next one: carry on
    // rather than circling back (a small arrival radius could otherwise orbit).
    glm::vec2 seg = xz(m_path[m_pathIndex + 1]) - t;
    const float len = glm::length(seg);
    if (len < 1e-4f) return true;
    seg /= len;
    const glm::vec2 rel = p - t;
    return glm::dot(rel, seg) > 0.0f && std::fabs(rel.x * seg.y - rel.y * seg.x) < 0.3f;
}

bool Agent::nextCrossing(glm::ivec2& from, glm::ivec2& to) const {
    const glm::ivec2 here = NavGrid::cellOf(m_feet);
    for (size_t i = 0; i + 1 < m_pathCells.size(); ++i) {
        if (m_pathCells[i] == here) {
            from = m_pathCells[i];
            to = m_pathCells[i + 1];
            return true;
        }
    }
    return false;
}

bool Agent::followPath(float dt, float speed, float accel, const NavGrid& nav, const ICollisionWorld& world,
                       const Physics& physics) {
    if (!hasPath()) {
        integrate(dt, glm::vec3(0.0f), accel, world, physics);
        return true;
    }
    glm::vec3 target = steerTarget(nav);
    glm::vec3 d = target - m_feet;
    d.y = 0.0f;
    if (reachedWaypoint()) {
        ++m_pathIndex;
        if (!hasPath()) {
            integrate(dt, glm::vec3(0.0f), accel, world, physics);
            return true;
        }
        target = steerTarget(nav);
        d = target - m_feet;
        d.y = 0.0f;
    }
    const float dist = glm::length(d);
    glm::vec3 dir = dist > 1e-4f ? d / dist : forward();

    // Pressing against an obstacle: sidestep (alternating sides) and stop
    // cutting corners until clear.
    m_noShortcuts = std::max(0.0f, m_noShortcuts - dt);
    if (m_stuck > 0.3f && m_detour <= 0.0f) {
        m_detour = 0.6f;
        m_detourSide = -m_detourSide;
        m_noShortcuts = 2.5f;
    }
    if (m_detour > 0.0f) {
        m_detour -= dt;
        const float a = 1.2f * m_detourSide; // ~70 degrees off the blocked heading
        dir = glm::vec3(dir.x * std::cos(a) - dir.z * std::sin(a), 0.0f, dir.x * std::sin(a) + dir.z * std::cos(a));
    }
    const bool last = m_pathIndex + 1 == m_path.size();
    const float v = last ? speed * std::clamp(dist / 1.0f, 0.3f, 1.0f) : speed; // ease into the goal
    integrate(dt, dir * v, accel, world, physics);
    turnTowards(dir, 7.0f, dt);
    return false;
}

void Agent::integrate(float dt, const glm::vec3& desired, float accel, const ICollisionWorld& world,
                      const Physics& physics) {
    const float blend = 1.0f - std::exp(-accel * dt);
    m_velocity.x += (desired.x - m_velocity.x) * blend;
    m_velocity.z += (desired.z - m_velocity.z) * blend;
    const float dy = m_velocity.y * dt - 0.5f * cfg::kGravity * dt * dt;
    m_velocity.y = std::max(m_velocity.y - cfg::kGravity * dt, -cfg::kTerminalVelocity);

    const glm::vec3 before = m_feet;
    const MoveResult r = physics.move(m_feet, {m_halfWidth, m_height}, glm::vec3(m_velocity.x * dt, dy, m_velocity.z * dt),
                                      m_grounded ? cfg::kStepHeight : 0.0f, m_grounded && m_velocity.y <= 0.0f, world);
    if (r.blockedX) m_velocity.x = 0.0f;
    if (r.blockedZ) m_velocity.z = 0.0f;
    if (r.grounded && m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    m_grounded = r.grounded;

    // Stuck: trying to move but making (almost) no progress.
    const float wanted = glm::length(glm::vec2(desired.x, desired.z)) * dt;
    const float moved = glm::length(xz(m_feet) - xz(before));
    if (wanted > 0.005f && moved < 0.25f * wanted) m_stuck += dt;
    else m_stuck = std::max(0.0f, m_stuck - 2.0f * dt);
}

void Agent::turnTowards(const glm::vec3& dir, float rate, float dt) {
    if (glm::dot(glm::vec2(dir.x, dir.z), glm::vec2(dir.x, dir.z)) < 1e-6f) return;
    const float target = std::atan2(dir.x, dir.z);
    float delta = std::fmod(target - m_yaw + 3.0f * kPi, 2.0f * kPi) - kPi;
    const float step = rate * dt;
    delta = std::clamp(delta, -step, step);
    m_yaw = std::fmod(m_yaw + delta, 2.0f * kPi);
}
