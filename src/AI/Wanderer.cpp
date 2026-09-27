// ---------------------------------------------------------------------------
// Wanderer.cpp
// ---------------------------------------------------------------------------
#include "AI/Wanderer.h"

#include "Core/Config.h"
#include "Render/EntityRenderer.h"
#include "World/ChunkManager.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHalfWidth = 0.26f;
constexpr float kHeight = 2.05f; ///< Collision height: just under a door opening (it ducks through).

inline glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }

struct Frame {
    glm::vec3 origin, side, up, fwd;
    glm::vec3 operator()(const glm::vec3& l) const { return origin + side * l.x + up * l.y + fwd * l.z; }
    glm::vec3 dir(const glm::vec3& l) const { return side * l.x + up * l.y + fwd * l.z; }
};
Frame frameOf(const glm::vec3& feet, float yaw) {
    return {feet, glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw)), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw))};
}

} // namespace

Wanderer::Wanderer(uint64_t seed) : Agent(kHalfWidth, kHeight), m_rng(seed) {}

void Wanderer::place(const glm::vec3& feet, int level, float yaw) {
    Agent::place(feet, level, yaw);
    m_state = State::Roaming;
    m_alert = 0.0f;
    m_timer = 0.0f;
    m_listenPause = 0.0f;
    m_doorWait = 0.0f;
    m_reach = 0.0f;
    pose();
}

glm::vec3 Wanderer::confront(const glm::vec3& playerFeet, const glm::vec3& playerForward) {
    const glm::vec3 fwd = glm::normalize(glm::vec3(playerForward.x, 0.0f, playerForward.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
    m_feet = glm::vec3(playerFeet.x, m_feet.y, playerFeet.z) + fwd * 0.7f;
    m_yaw = std::atan2(-fwd.x, -fwd.z); // facing the player
    m_velocity = glm::vec3(0.0f);
    m_reach = 1.0f;
    m_stride = 0.0f;
    m_headCock = 0.3f;
    clearPath();
    pose();
    return m_head;
}

// ---- Hearing ------------------------------------------------------------------------------

bool Wanderer::listen(const std::vector<NoiseEvent>& noises, const NavGrid& nav) {
    const NoiseEvent* heard = nullptr;
    float best = 0.0f;
    for (const NoiseEvent& n : noises) {
        if (world::levelOf(n.position.y) != m_level) continue; // the slab swallows it
        const float dist = glm::length(n.position - m_head);
        if (dist < 2.5f) continue;                             // its own doors and steps
        const bool clear = nav.lineOfSight(m_level, xz(n.position), xz(m_head));
        const float radius = n.radius * (clear ? 1.0f : 0.5f);
        if (dist >= radius) continue;
        const float strength = 1.0f - dist / radius;
        if (strength > best) {
            best = strength;
            heard = &n;
        }
    }
    if (!heard) return false;

    // Blind: it knows roughly where the sound came from, less precisely from afar.
    const float dist = glm::length(heard->position - m_head);
    const float angle = m_rng.range(0.0f, 2.0f * kPi);
    const float error = m_rng.range(0.0f, dist * 0.12f);
    m_target = glm::vec3(heard->position.x + std::cos(angle) * error, world::levelFloorY(m_level),
                         heard->position.z + std::sin(angle) * error);
    m_alert = std::min(1.0f, std::max(m_alert, 0.3f + best));

    glm::vec3 dir = m_target - m_feet;
    dir.y = 0.0f;
    if (glm::length(dir) > 1e-3f) m_heardDir = glm::normalize(dir);
    // Caught off guard, it freezes for a moment and cocks its head to listen.
    if (m_state == State::Roaming || m_state == State::Searching) m_listenPause = m_rng.range(0.4f, 0.9f);
    const bool loud = best > 0.45f || m_alert > 0.85f;
    m_state = loud || m_state == State::Hunting ? State::Hunting : State::Investigating;
    m_replan = 0.0f;
    return true;
}

// ---- Movement -----------------------------------------------------------------------------

void Wanderer::pickRoamTarget(const NavGrid& nav, const glm::vec3& playerFeet) {
    // A short random walk over the cell graph. Most steps lean towards the
    // player: it has not heard them, but somehow it always drifts their way.
    glm::ivec2 cell = NavGrid::cellOf(m_feet);
    const glm::ivec2 player = NavGrid::cellOf(playerFeet);
    const NavProfile profile;
    const glm::ivec2 steps[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    auto distance = [&player](const glm::ivec2& c) { return std::abs(c.x - player.x) + std::abs(c.y - player.y); };
    for (int n = m_rng.rangeInt(3, 7); n > 0; --n) {
        const bool drift = m_rng.chance(cfg::kWandererDrift);
        const int first = m_rng.rangeInt(0, 3);
        glm::ivec2 next = cell;
        for (int k = 0; k < 4; ++k) {
            const glm::ivec2 nb = cell + steps[(first + k) % 4];
            if (!nav.canStep(m_level, cell, nb, profile)) continue;
            if (!drift) {
                next = nb;
                break;
            }
            if (next == cell || distance(nb) < distance(next)) next = nb;
        }
        cell = next;
    }
    const glm::vec3 c = NavGrid::cellCenter(cell, m_level);
    planTo(nav, c + glm::vec3(m_rng.range(-1.5f, 1.5f), 0.0f, m_rng.range(-1.5f, 1.5f)), profile);
}

bool Wanderer::handleDoors(float dt, const NavGrid& nav, ChunkManager& chunks, const Physics& physics) {
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
    if (glm::length(xz(c) - xz(m_feet)) > 1.4f) return false;
    if (m_doorWait > 3.0f) return false; // something holds it shut: shove on regardless

    // A closed door ahead: it fumbles it open (loudly) and waits for it to swing.
    if (door->state() == Door::State::Closed || door->state() == Door::State::Closing) door->toggle(m_feet);
    m_doorWait += dt;
    integrate(dt, glm::vec3(0.0f), 8.0f, chunks, physics);
    turnTowards(c - m_feet, 3.0f, dt);
    return true;
}

bool Wanderer::update(float dt, const PlayerView& player, const NavGrid& nav, ChunkManager& chunks,
                      const Physics& physics, const std::vector<NoiseEvent>& noises, std::vector<EntitySound>& sounds) {
    if (!m_active) return false;
    listen(noises, nav);
    m_alert = std::max(0.0f, m_alert - 0.04f * dt);

    if (m_listenPause > 0.0f) {
        m_listenPause -= dt;
        integrate(dt, glm::vec3(0.0f), 8.0f, chunks, physics);
        turnTowards(m_heardDir, 2.5f, dt);
    } else {
        switch (m_state) {
        case State::Roaming:
            if (!hasPath()) {
                // Idles a moment between aimless walks.
                m_timer -= dt;
                integrate(dt, glm::vec3(0.0f), 6.0f, chunks, physics);
                if (m_timer <= 0.0f) {
                    pickRoamTarget(nav, player.feet);
                    m_timer = m_rng.range(0.5f, 3.0f);
                }
            } else if (!handleDoors(dt, nav, chunks, physics) &&
                       (followPath(dt, cfg::kWandererRoamSpeed, 4.0f, nav, chunks, physics) || m_stuck > 1.5f)) {
                clearPath();
            }
            break;

        case State::Investigating:
        case State::Hunting: {
            const bool hunting = m_state == State::Hunting;
            m_replan -= dt;
            if (m_replan <= 0.0f) {
                NavProfile profile;
                profile.doorCost = 0.5f;
                planTo(nav, m_target, profile);
                m_replan = hunting ? 0.8f : 2.0f;
            }
            const float speed = hunting ? cfg::kWandererHuntSpeed : cfg::kWandererSearchSpeed;
            if (!handleDoors(dt, nav, chunks, physics)) {
                const bool arrived = followPath(dt, speed, hunting ? 6.0f : 4.0f, nav, chunks, physics);
                if (arrived || glm::length(xz(m_target) - xz(m_feet)) < 0.8f || m_stuck > 2.5f) {
                    m_state = State::Searching;
                    m_timer = m_rng.range(5.0f, 9.0f);
                    clearPath();
                }
            }
            break;
        }

        case State::Searching:
            // Gropes around the spot where the sound was.
            m_timer -= dt;
            if (!hasPath() || followPath(dt, 0.8f, 4.0f, nav, chunks, physics)) {
                planTo(nav, m_target + glm::vec3(m_rng.range(-2.5f, 2.5f), 0.0f, m_rng.range(-2.5f, 2.5f)), NavProfile{});
            }
            if (m_timer <= 0.0f) {
                m_state = State::Roaming;
                clearPath();
                m_timer = m_rng.range(1.0f, 3.0f);
            }
            break;
        }
    }

    // Too tall for the doorways: it hunches down to squeeze through them.
    bool underHeader = false;
    glm::ivec2 from, to;
    if (nextCrossing(from, to)) {
        int gx, gz;
        world::EdgeAxis axis;
        NavGrid::edgeBetween(from, to, gx, gz, axis);
        const world::EdgeType type = nav.edgeType(m_level, gx, gz, axis);
        underHeader = type != world::EdgeType::Open && glm::length(xz(nav.crossing(m_level, from, to)) - xz(m_feet)) < 1.6f;
    }
    m_duck += ((underHeader ? 1.0f : 0.0f) - m_duck) * (1.0f - std::exp(-4.0f * dt));

    animate(dt, sounds);

    const glm::vec3 toPlayer = player.feet - m_feet;
    return player.level == m_level && glm::length(xz(toPlayer)) < cfg::kCatchDistance && std::fabs(toPlayer.y) < 1.0f;
}

// ---- Animation -------------------------------------------------------------------------------

void Wanderer::animate(float dt, std::vector<EntitySound>& sounds) {
    m_animTime += dt;
    const float speed = glm::length(xz(m_velocity));
    m_stride += (std::min(speed / cfg::kWandererHuntSpeed, 1.0f) * 0.5f - m_stride) * (1.0f - std::exp(-5.0f * dt));

    // One footfall per half gait cycle: heavy, dragging, and audible.
    const float before = m_phase;
    m_phase += dt * (speed * 2.4f + (speed > 0.05f ? 0.6f : 0.0f));
    if (std::floor(m_phase / kPi) != std::floor(before / kPi) && m_stride > 0.04f) {
        sounds.push_back({EntitySound::Type::WandererStep, m_feet, std::min(1.0f, speed / cfg::kWandererHuntSpeed + 0.3f)});
    }
    m_phase = std::fmod(m_phase, 2.0f * kPi * 64.0f);

    const bool groping = m_state != State::Roaming;
    const float reachTarget = groping ? 1.0f : m_listenPause > 0.0f ? 0.6f : 0.0f;
    m_reach += (reachTarget - m_reach) * (1.0f - std::exp(-3.0f * dt));
    // Cocks its head towards what it heard; otherwise it lolls.
    const Frame F = frameOf(m_feet, m_yaw);
    const float cock = m_listenPause > 0.0f ? glm::dot(m_heardDir, F.side) * 0.9f : 0.25f * std::sin(m_animTime * 0.45f);
    m_headCock += (cock - m_headCock) * (1.0f - std::exp(-4.0f * dt));
    pose();
}

void Wanderer::pose() {
    const Frame F = frameOf(m_feet, m_yaw);
    const float t = m_animTime;
    const float stoop = 0.35f + 0.4f * m_reach + 0.3f * m_duck;
    const float crouch = 0.28f * m_duck; // knees bend, the whole body sinks
    m_rig.clear();
    m_mouth.clear();

    // ---- Torso: a starved, stooped spine.
    const float sway = 0.03f * std::sin(m_phase);
    const glm::vec3 pelvis{sway, 1.10f - crouch + 0.02f * std::cos(2.0f * m_phase), 0.0f};
    const glm::vec3 belly{sway * 0.5f, 1.37f - crouch, 0.03f + stoop * 0.05f};
    const glm::vec3 chest{0.0f, 1.64f - crouch, 0.06f + stoop * 0.14f};
    const glm::vec3 neck{0.0f, 1.86f - crouch, 0.14f + stoop * 0.22f};
    m_rig.limb(F(pelvis), F(belly), 0.12f, 0.085f);
    m_rig.limb(F(belly), F(chest), 0.085f, 0.14f);
    m_rig.limb(F(chest), F(neck), 0.12f, 0.045f);

    // ---- Head: long, smooth and eyeless, with a gaping vertical mouth.
    const glm::vec3 headBase = neck + glm::vec3(0.0f, 0.06f, 0.03f);
    const glm::vec3 headDir = glm::normalize(glm::vec3(m_headCock, 1.0f, 0.35f + stoop * 0.6f));
    const glm::vec3 headTip = headBase + headDir * 0.42f;
    m_rig.limb(F(neck), F(headBase), 0.045f, 0.075f);
    m_rig.limb(F(headBase), F(headTip), 0.085f, 0.125f);
    m_head = F(glm::mix(headBase, headTip, 0.5f));
    const glm::vec3 faceDir = glm::normalize(glm::vec3(0.0f, -headDir.z, headDir.y)); // forward, square to the skull
    const glm::vec3 mouthTop = glm::mix(headBase, headTip, 0.42f) + faceDir * 0.1f;
    const float gape = 0.07f + 0.03f * std::sin(t * 3.1f);
    m_mouth.limb(F(mouthTop), F(mouthTop - headDir * gape), 0.022f, 0.028f);

    for (float s : {-1.0f, 1.0f}) {
        // ---- Arms: hanging, or reaching ahead into the dark, fingers feeling the air.
        const glm::vec3 shoulder = chest + glm::vec3(0.20f * s, 0.12f, 0.0f);
        const float armSwing = std::sin(m_phase + (s > 0.0f ? kPi : 0.0f));
        const glm::vec3 hang{0.30f * s, 0.92f, 0.22f + 0.12f * armSwing * m_stride};
        const glm::vec3 reach{0.24f * s, 1.42f + 0.08f * std::sin(t * 1.3f + s), 0.66f + 0.05f * std::sin(t * 0.9f + 2.0f * s)};
        const glm::vec3 hand = glm::mix(hang, reach, m_reach);
        const rig::TwoBone arm = rig::solveTwoBone(F(shoulder), F(hand), 0.44f, 0.48f, F.dir(glm::normalize(glm::vec3(0.6f * s, -1.0f, -0.3f))));
        m_rig.limb(F(shoulder), arm.joint, 0.045f, 0.032f);
        m_rig.limb(arm.joint, arm.end, 0.032f, 0.024f);
        const glm::vec3 fore = glm::normalize(arm.end - arm.joint);
        for (int f = 0; f < 4; ++f) {
            const float spread = (static_cast<float>(f) - 1.5f) * 0.28f;
            const float curl = 0.3f + 0.25f * std::sin(t * 2.3f + static_cast<float>(f) + s);
            const glm::vec3 d1 = glm::normalize(fore + F.side * spread - F.up * 0.15f);
            const glm::vec3 d2 = glm::normalize(d1 - F.up * curl);
            const glm::vec3 k1 = arm.end + d1 * 0.11f;
            m_rig.limb(arm.end, k1, 0.012f, 0.009f);
            m_rig.limb(k1, k1 + d2 * 0.09f, 0.009f, 0.005f);
        }

        // ---- Legs: a limping, dragging gait (the left foot barely lifts).
        const glm::vec3 hip = pelvis + glm::vec3(0.11f * s, -0.03f, 0.0f);
        const float lp = m_phase + (s > 0.0f ? 0.0f : kPi);
        const float lift = (s > 0.0f ? 0.13f : 0.03f) * std::max(0.0f, std::cos(lp)) * (m_stride / 0.5f);
        const glm::vec3 foot{0.14f * s, 0.03f + lift, m_stride * std::sin(lp) * (s > 0.0f ? 1.0f : 0.7f)};
        const rig::TwoBone leg = rig::solveTwoBone(F(hip), F(foot), 0.50f, 0.56f, F.fwd);
        m_rig.limb(F(hip), leg.joint, 0.07f, 0.045f);
        m_rig.limb(leg.joint, leg.end, 0.045f, 0.03f);
        m_rig.limb(leg.end, leg.end + F.fwd * 0.17f - F.up * 0.02f, 0.035f, 0.02f);
    }
}

void Wanderer::buildGeometry(EntityDrawList& list) const {
    if (!m_active) return;
    m_rig.appendMesh(list.lit, MaterialId::Flesh, 10);
    m_mouth.appendMesh(list.lit, MaterialId::DarkPlastic, 8);
}
