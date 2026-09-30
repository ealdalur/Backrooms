// ---------------------------------------------------------------------------
// Agent.cpp
// ---------------------------------------------------------------------------
#include "AI/Agent.h"

#include "Actors/Door.h"
#include "Core/Config.h"
#include "Math/Random.h"
#include "Render/EntityRenderer.h"
#include "World/ChunkManager.h"
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
constexpr float kDoorStandoff = 1.45f;   ///< Clear of a door panel's arc (0.9 m panel + a body's half width).
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
    m_doorWait = 0.0f;
    clearPath();
}

bool Agent::planTo(const NavGrid& nav, const glm::vec3& goal, const NavProfile& profile, int goalLevel) {
    // Already on a climb: finish it first; the rest is planned at the far end.
    if (m_climbing) {
        m_finalGoal = goal;
        m_finalLevel = goalLevel;
        m_finalProfile = profile;
        return true;
    }
    const glm::ivec2 here = NavGrid::cellOf(m_feet);
    // Standing in a stairwell (placed there, or a path was dropped mid-climb):
    // on along its stair route to the end on the right storey.
    if (nav.isStairwell(m_level, here)) {
        StairLink link;
        for (int dir : {1, -1}) {
            if (!nav.findStairLink(m_level, dir, here, link, 1) || link.cell != here) continue;
            m_finalGoal = goal;
            m_finalLevel = goalLevel;
            m_finalProfile = profile;
            joinRoute(link, goalLevel > link.lower, false);
            return true;
        }
    }
    const glm::ivec2 goalCell = NavGrid::cellOf(goal);
    if (goalLevel == m_level && nav.isStairwell(goalLevel, goalCell)) {
        // The goal is on the stairs (the player is climbing them): in at this
        // storey's entrance and along the flights after it.
        for (int dir : {1, -1}) {
            StairLink link;
            if (!nav.findStairLink(m_level, dir, goalCell, link, 1) || link.cell != goalCell) continue;
            if (!planFlat(nav, dir > 0 ? link.route.front() : link.route.back(), profile)) break;
            m_link = std::move(link);
            m_climbDir = dir;
            m_climbPending = true;
            m_finalGoal = goal;
            m_finalLevel = goalLevel;
            m_finalProfile = profile;
            return true;
        }
    }
    if (goalLevel == m_level) {
        m_climbPending = false;
        return planFlat(nav, goal, profile);
    }

    // Another storey: to the nearest stairwell going that way, then up (or down) it.
    const int dir = goalLevel > m_level ? 1 : -1;
    StairLink link;
    if (!nav.findStairLink(m_level, dir, here, link)) {
        clearPath();
        return false;
    }
    const glm::vec3 entry = dir > 0 ? link.route.front() : link.route.back();
    if (!planFlat(nav, entry, profile)) {
        clearPath();
        return false;
    }
    m_link = std::move(link);
    m_climbDir = dir;
    m_climbPending = true;
    m_finalGoal = goal;
    m_finalLevel = goalLevel;
    m_finalProfile = profile;
    return true;
}

void Agent::joinRoute(const StairLink& link, bool toUpper, bool fromStart) {
    m_link = link;
    m_climbDir = toUpper ? 1 : -1;
    std::vector<glm::vec3> route = link.route;
    if (!toUpper) std::reverse(route.begin(), route.end());
    size_t first = 1; // from its start: already standing on the first point
    if (!fromStart) { // somewhere along it: from the nearest point at about this height on
        float best = 1e9f;
        for (size_t i = 0; i < route.size(); ++i) {
            const glm::vec3 d = route[i] - m_feet;
            const float score = glm::length(glm::vec2(d.x, d.z)) + 2.0f * std::fabs(d.y);
            if (score < best) {
                best = score;
                first = i;
            }
        }
    }
    m_path.assign(route.begin() + static_cast<std::ptrdiff_t>(std::min(first, route.size() - 1)), route.end());
    m_pathIndex = 0;
    m_pathCells = {link.outside, link.cell, link.outside}; // for the doors on the way in and out
    m_climbPending = false;
    m_climbing = true;
}

bool Agent::nextLeg(const NavGrid& nav) {
    if (m_climbPending) {
        joinRoute(m_link, m_climbDir > 0, true);
        return hasPath();
    }
    if (m_climbing) {
        m_climbing = false;
        const NavProfile profile = m_finalProfile; // copied: planTo overwrites it
        return planTo(nav, m_finalGoal, profile, m_finalLevel) && hasPath();
    }
    return false;
}

void Agent::updateLevel() {
    const float base = world::levelFloorY(m_level);
    const float band = cfg::kLevelSwitchBand * world::kLevelHeight;
    if (m_feet.y > base + band) ++m_level;
    else if (m_feet.y < base - band) --m_level;
}

bool Agent::handleDoors(float dt, const NavGrid& nav, ChunkManager& chunks, const Physics& physics) {
    glm::ivec2 from, to;
    int gx, gz;
    world::EdgeAxis axis;
    if (!nextCrossing(from, to) || !NavGrid::edgeBetween(from, to, gx, gz, axis)) return false;
    Door* door = chunks.doorOnEdge(m_level, gx, gz, axis);
    if (!door || door->state() == Door::State::Open) {
        m_doorWait = 0.0f;
        return false;
    }
    const glm::vec3 c = nav.crossing(m_level, from, to);
    // Once waiting for a door it keeps waiting from a step further back.
    if (glm::length(xz(c) - xz(m_feet)) > (m_doorWait > 0.0f ? 1.9f : 1.4f)) {
        m_doorWait = 0.0f;
        return false;
    }
    if (m_doorWait > 3.0f) return false; // something holds it shut: shove on regardless

    // A closed door ahead: open it and wait for it to swing.
    if (door->state() == Door::State::Closed || door->state() == Door::State::Closing) door->toggle(m_feet);
    m_doorWait += dt;
    glm::vec3 hold(0.0f);
    if (door->swingsToward(m_feet)) { // it opens this way: step back out of its arc
        const glm::vec3 through(static_cast<float>(to.x - from.x), 0.0f, static_cast<float>(to.y - from.y));
        glm::vec3 d = c - through * kDoorStandoff - m_feet;
        d.y = 0.0f;
        if (glm::length(d) > 0.1f) hold = glm::normalize(d) * 1.2f;
    }
    integrate(dt, hold, 8.0f, chunks, physics);
    turnTowards(c - m_feet, 3.0f, dt);
    return true;
}

bool Agent::planFlat(const NavGrid& nav, const glm::vec3& goal, const NavProfile& profile) {
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
    // String pulling: skip ahead while the next waypoint is directly reachable
    // (never on the stairs: the route goes round the flights and the landing).
    if (m_noShortcuts <= 0.0f && !m_climbing) {
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
    if (!hasPath() && !nextLeg(nav)) {
        integrate(dt, glm::vec3(0.0f), accel, world, physics);
        return true;
    }
    glm::vec3 target = steerTarget(nav);
    glm::vec3 d = target - m_feet;
    d.y = 0.0f;
    if (reachedWaypoint()) {
        ++m_pathIndex;
        if (!hasPath() && !nextLeg(nav)) {
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
    const bool last = m_pathIndex + 1 == m_path.size() && !usingStairs();
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
    updateLevel();

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

// ---- Vitals ----------------------------------------------------------------------------------

void Agent::applyShock(float damage) {
    if (!m_active || dying()) return;
    m_shock = 1.0f;
    m_sinceShock = 0.0f;
    m_health -= damage * m_vulnerability;
    if (m_health <= 0.0f) {
        m_health = 0.0f;
        m_dissolve = 0.0f;
        m_justDied = true;
        m_velocity = glm::vec3(0.0f);
    }
}

void Agent::vaporizeNow() {
    if (!dying()) return;
    m_dissolve = 1.0f;
    m_destroyed = true;
    m_active = false;
    m_embers.clear();
}

Agent::VitalSigns Agent::updateVitals(float dt) {
    VitalSigns signs;
    m_vitalTime += dt;
    m_sinceShock += dt;
    // The arc re-connects every frame it holds; once it lets go the spasms die away in a moment.
    m_shock = std::max(0.0f, m_shock - 4.0f * dt);
    if (!dying() && m_sinceShock > cfg::kShockRegenDelay) m_health = std::min(1.0f, m_health + cfg::kShockRegenRate * dt);

    if (shocked() && !dying()) {
        m_painTimer -= dt;
        if (m_painTimer <= 0.0f) {
            signs.pain = true;
            m_painTimer = 0.8f + 0.6f * rnd::toUnit(rnd::hashCombine(m_emberSeed++, 0x9A17ull));
        }
    } else {
        m_painTimer = 0.0f; // the next shock screams at once
    }
    if (m_justDied) {
        signs.died = true;
        m_justDied = false;
    }

    // ---- Vaporising: the body sheds glowing embers as it comes apart.
    for (Ember& e : m_embers) {
        e.age += dt;
        e.vel += glm::vec3(0.0f, 0.9f, 0.0f) * dt; // heat carries them up
        e.vel *= std::exp(-1.2f * dt);
        e.pos += e.vel * dt;
    }
    m_embers.erase(std::remove_if(m_embers.begin(), m_embers.end(), [](const Ember& e) { return e.age >= e.life; }),
                   m_embers.end());
    if (dying()) {
        m_dissolve = std::min(1.0f, m_dissolve + dt / cfg::kVaporizeTime);
        const std::vector<CreatureRig::Limb>& limbs = body().limbs();
        m_emberTimer -= dt;
        while (m_emberTimer <= 0.0f && !limbs.empty() && m_dissolve < 0.9f && m_embers.size() < 220) {
            m_emberTimer += 1.0f / 70.0f;
            rnd::Rng rng(rnd::hashCombine(m_emberSeed++, 0xE3B3ull));
            const CreatureRig::Limb& l = limbs[rng.next() % limbs.size()];
            const float t = rng.nextFloat();
            const glm::vec3 jitter(rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f), rng.range(-1.0f, 1.0f));
            Ember e;
            e.pos = glm::mix(l.a, l.b, t) + jitter * glm::mix(l.ra, l.rb, t);
            e.vel = glm::vec3(rng.range(-0.25f, 0.25f), rng.range(0.1f, 0.6f), rng.range(-0.25f, 0.25f));
            e.age = 0.0f;
            e.life = rng.range(0.6f, 1.6f);
            e.size = rng.range(0.008f, 0.022f);
            m_embers.push_back(e);
        }
        if (m_emberTimer < 0.0f) m_emberTimer = 0.0f;
        if (m_dissolve >= 1.0f) {
            // Nothing left of it.
            m_destroyed = true;
            m_active = false;
            m_embers.clear();
        }
    }
    return signs;
}

void Agent::convulse(CreatureRig& rig) const {
    // Violent while the arc holds; a dying body keeps shuddering as it goes.
    const float amount = std::max(m_shock, dying() ? 0.6f * (1.0f - m_dissolve) : 0.0f);
    if (amount <= 0.0f) return;
    const float t = m_vitalTime;
    const float a = 0.045f * amount;
    rig.warp([t, a](const glm::vec3& p) {
        return p + a * glm::vec3(std::sin(p.y * 31.0f + t * 57.0f) + 0.5f * std::sin(p.z * 17.0f + t * 91.0f),
                                 0.6f * std::sin(p.x * 27.0f + p.z * 13.0f + t * 63.0f),
                                 std::sin(p.z * 29.0f + t * 49.0f) + 0.5f * std::sin(p.x * 19.0f + t * 83.0f));
    });
}

void Agent::buildEmbers(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const {
    for (const Ember& e : m_embers) {
        const float life = e.age / e.life;
        list.addSprite(e.pos, e.size * (1.0f - 0.6f * life), EntityDrawList::Ember, 1.0f - life, camRight, camUp);
    }
}
