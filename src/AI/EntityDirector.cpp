// ---------------------------------------------------------------------------
// EntityDirector.cpp
// ---------------------------------------------------------------------------
#include "AI/EntityDirector.h"

#include "Core/Config.h"
#include "Core/ConsoleLog.h"
#include "Physics/Physics.h"
#include "Render/EntityRenderer.h"
#include "World/ChunkManager.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530718f;
inline glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }
inline float yawToward(const glm::vec3& from, const glm::vec3& to) { return std::atan2(to.x - from.x, to.z - from.z); }

const char* entityName(EntityKind kind) { return kind == EntityKind::Stalker ? "Stalker" : "Wanderer"; }
} // namespace

EntityDirector::EntityDirector(const WorldGenerator& generator, const ChunkManager& chunks, uint64_t seed)
    : m_nav(generator, chunks),
      m_rng(rnd::hashCombine(seed, 0xE417'1735ull)),
      m_stalker(rnd::hashCombine(seed, 0x57A1'4E12ull)),
      m_wanderer(rnd::hashCombine(seed, 0x3A4D'E12Eull)),
      m_stalkerTimer(cfg::kStalkerFirstSpawn),
      m_wandererTimer(cfg::kWandererFirstSpawn) {}

void EntityDirector::setEnabled(bool enabled) {
    setEnabled(EntityKind::Stalker, enabled);
    setEnabled(EntityKind::Wanderer, enabled);
}

void EntityDirector::setEnabled(EntityKind kind, bool enabled) {
    if (kind == EntityKind::Stalker) {
        m_stalkerEnabled = enabled && !m_stalkerGone;
        if (!enabled) m_stalker.deactivate();
    } else {
        m_wandererEnabled = enabled && !m_wandererGone;
        if (!enabled) m_wanderer.deactivate();
    }
}

void EntityDirector::spawnAt(EntityKind kind, const glm::vec3& feet, int level, float yaw, bool hunt) {
    if (gone(kind)) return;
    if (kind == EntityKind::Stalker) {
        m_stalker.place(feet, level, yaw);
        if (hunt) m_stalker.provoke();
    } else {
        m_wanderer.place(feet, level, yaw);
    }
}

glm::vec3 EntityDirector::confront(EntityKind kind, const glm::vec3& playerFeet, const glm::vec3& playerForward) {
    m_holding = true;
    return kind == EntityKind::Stalker ? m_stalker.confront(playerFeet, playerForward)
                                       : m_wanderer.confront(playerFeet, playerForward);
}

void EntityDirector::scatter() {
    m_holding = false;
    m_stalker.deactivate();
    m_wanderer.deactivate();
    m_stalkerTimer = m_rng.range(25.0f, 45.0f);
    m_wandererTimer = m_rng.range(15.0f, 30.0f);
    m_catch.reset();
}

std::optional<EntityKind> EntityDirector::takeCatch() {
    std::optional<EntityKind> c = m_catch;
    m_catch.reset();
    return c;
}

bool EntityDirector::findSpawn(const PlayerView& view, float minDist, float maxDist, bool preferDark,
                               const Physics& physics, const ChunkManager& chunks, glm::vec3& out,
                               const glm::vec3* ahead) {
    const int level = view.level;
    float bestScore = 1e9f;
    for (int i = 0; i < 60; ++i) {
        // With a preferred direction, most tries fan out within ~70 degrees of
        // it; the rest (and every try otherwise) go all the way round.
        const bool fan = ahead && i < 40;
        const float angle = fan ? std::atan2(ahead->z, ahead->x) + m_rng.range(-1.2f, 1.2f) : m_rng.range(0.0f, kTwoPi);
        const float r = m_rng.range(minDist, maxDist);
        const glm::vec3 probe = view.feet + glm::vec3(std::cos(angle) * r, 0.0f, std::sin(angle) * r);
        const glm::ivec2 cell = NavGrid::cellOf(probe);
        if (!m_nav.walkable(level, cell)) continue;
        glm::vec3 p = NavGrid::cellCenter(cell, level) + glm::vec3(m_rng.range(-1.6f, 1.6f), 0.0f, m_rng.range(-1.6f, 1.6f));
        if (!physics.isFree(Physics::bodyBox(p + glm::vec3(0.0f, 0.01f, 0.0f), {0.3f, 2.2f}), chunks)) continue;
        if (m_nav.lineOfSight(level, xz(view.eye), xz(p))) continue; // never materialise in plain sight
        const float score = preferDark ? m_nav.cellBrightness(level, cell) : 0.0f;
        if (score < bestScore) {
            bestScore = score;
            out = p;
            if (!preferDark) break;
        }
    }
    return bestScore < 1e8f;
}

bool EntityDirector::manageLifetime(Agent& agent, float& timer, float& away, float dt, const PlayerView& view,
                                    const ChunkManager& chunks, bool canVanish, float maxDistance) {
    // On another storey it has to walk (the stairs): only if it cannot find
    // its way for a long while does it give up and resurface near the player.
    const bool otherLevel = agent.level() != view.level;
    away = otherLevel ? away + dt : 0.0f;
    const bool stranded = away > cfg::kFollowTimeout;
    const bool gone = stranded || (!otherLevel && glm::length(agent.feet() - view.feet) > maxDistance) ||
                      !chunks.isLoadedAt(agent.feet(), agent.level()) ||
                      agent.feet().y < world::levelFloorY(agent.level()) - 3.0f ||
                      (agent.stuckTime() > 4.0f && !agent.usingStairs());
    if (!gone || !canVanish) return false;
    if (stranded) {
        con::line("ENTITY", std::string("The ") + (&agent == &m_stalker ? "Stalker" : "Wanderer") +
                                " found no way to the player's storey: it resurfaces near them");
    }
    agent.deactivate();
    away = 0.0f;
    timer = stranded ? m_rng.range(1.0f, 3.0f) : m_rng.range(3.0f, 7.0f);
    return true;
}

void EntityDirector::update(float dt, const Camera& camera, float aspect, const glm::vec3& playerFeet, int playerLevel,
                            bool viewBlocked, ChunkManager& chunks, const Physics& physics,
                            const std::vector<NoiseEvent>& noises) {
    m_sounds.clear();
    m_disturbances.clear();

    // ---- Vaporised entities are removed from the game for good.
    auto retire = [this](Agent& agent, bool& gone, bool& enabled, EntityKind kind) {
        if (gone || !agent.destroyed()) return;
        gone = true;
        enabled = false;
        m_vaporised.push_back(kind);
        con::spoiler("ENTITY", std::string("The ") + entityName(kind) + " has been vaporised. It will not return.");
    };
    retire(m_stalker, m_stalkerGone, m_stalkerEnabled, EntityKind::Stalker);
    retire(m_wanderer, m_wandererGone, m_wandererEnabled, EntityKind::Wanderer);

    // ---- The player's view: slightly narrower than the screen, so something
    //      glimpsed at the very edge is not "seen".
    const glm::vec3 moved(playerFeet.x - m_lastFeet.x, 0.0f, playerFeet.z - m_lastFeet.z);
    if (dt > 0.0f && glm::length(moved) > 0.5f * dt && glm::length(moved) < 1.0f) { // walking (and not a teleport)
        m_travel = glm::normalize(glm::mix(m_travel, glm::normalize(moved), 1.0f - std::exp(-dt / 1.5f)) + glm::vec3(1e-4f, 0.0f, 0.0f));
    } else if (glm::length(moved) >= 1.0f) {
        m_travel = glm::normalize(glm::vec3(camera.forward().x, 0.0f, camera.forward().z) + glm::vec3(1e-4f, 0.0f, 0.0f));
    }
    m_lastFeet = playerFeet;
    m_view.eye = camera.position;
    m_view.feet = playerFeet;
    m_view.forward = camera.forward();
    m_view.level = playerLevel;
    m_view.viewBlocked = viewBlocked;
    Camera narrowed = camera;
    narrowed.fovYDegrees *= 0.92f;
    m_view.frustum.update(narrowed.projectionMatrix(aspect * 0.95f) * narrowed.viewMatrix());

    if (!m_holding) {
        // ---- The Stalker. (Stared down too often, it bolts far away on its own - see Stalker.)
        if (m_stalkerEnabled) {
            if (m_stalker.active()) {
                if (!manageLifetime(m_stalker, m_stalkerTimer, m_stalkerAway, dt, m_view, chunks,
                                    !m_stalker.seen() && !m_stalker.dying(), cfg::kStalkerRelocateDist) &&
                    m_stalker.update(dt, m_view, m_nav, chunks, physics, m_sounds)) {
                    m_catch = EntityKind::Stalker;
                }
            } else if ((m_stalkerTimer -= dt) <= 0.0f) {
                glm::vec3 p;
                if (findSpawn(m_view, 22.0f, 40.0f, true, physics, chunks, p)) {
                    m_stalker.place(p, playerLevel, yawToward(p, playerFeet));
                } else {
                    m_stalkerTimer = 2.0f;
                }
            }
        }

        // ---- The Wanderer.
        if (m_wandererEnabled) {
            if (m_wanderer.active()) {
                if (!manageLifetime(m_wanderer, m_wandererTimer, m_wandererAway, dt, m_view, chunks, !m_wanderer.dying(),
                                    cfg::kWandererRelocateDist) &&
                    m_wanderer.update(dt, m_view, m_nav, chunks, physics, noises, m_sounds)) {
                    m_catch = EntityKind::Wanderer;
                }
            } else if ((m_wandererTimer -= dt) <= 0.0f) {
                // Somewhere the player is heading, so they walk into its part of the building.
                glm::vec3 p;
                if (findSpawn(m_view, cfg::kWandererSpawnMin, cfg::kWandererSpawnMax, false, physics, chunks, p, &m_travel)) {
                    m_wanderer.place(p, playerLevel, m_rng.range(0.0f, kTwoPi));
                } else {
                    m_wandererTimer = 2.0f;
                }
            }
        }
    }

    // ---- Outputs.
    if (m_stalker.active() && m_stalker.level() == playerLevel) m_disturbances.push_back(m_stalker.lightDisturbance());

    float target = 0.0f;
    if (m_stalker.active() && !m_stalker.dying() && m_stalker.level() == playerLevel) {
        const float d = glm::length(m_stalker.feet() - playerFeet);
        if (m_stalker.seen()) target = std::max(target, 0.55f + 0.45f * std::clamp(1.0f - d / 15.0f, 0.0f, 1.0f));
        else if (d < 12.0f) target = std::max(target, 0.55f * (1.0f - d / 12.0f));
    }
    if (m_wanderer.active() && !m_wanderer.dying() && m_wanderer.level() == playerLevel) {
        const float d = glm::length(m_wanderer.feet() - playerFeet);
        if (d < 16.0f) target = std::max(target, 0.6f * (1.0f - d / 16.0f));
    }
    if (m_holding) target = 1.0f;
    const float rate = target > m_fear ? 2.5f : 0.5f;
    m_fear += (target - m_fear) * (1.0f - std::exp(-rate * dt));
}

void EntityDirector::buildDrawList(EntityDrawList& list, const Camera& camera) const {
    const glm::vec3 right = camera.right();
    const glm::vec3 up = glm::normalize(glm::cross(right, camera.forward()));
    m_stalker.buildGeometry(list, right, up);
    m_wanderer.buildGeometry(list, right, up);
}

EntityAudioState EntityDirector::audioState() const {
    EntityAudioState s;
    s.stalkerActive = m_stalker.active() && !m_stalker.dying();
    s.stalkerPosition = m_stalker.feet() + glm::vec3(0.0f, 1.0f, 0.0f);
    s.wandererActive = m_wanderer.active() && !m_wanderer.dying(); // its voice dies with it
    s.wandererHead = m_wanderer.headPosition();
    s.wandererAgitation = m_wanderer.agitation();
    return s;
}

float EntityDirector::stalkerDistance() const {
    if (!m_stalker.active() || m_stalker.level() != m_view.level) return -1.0f;
    return glm::length(m_stalker.feet() - m_view.feet);
}

float EntityDirector::wandererDistance() const {
    if (!m_wanderer.active() || m_wanderer.level() != m_view.level) return -1.0f;
    return glm::length(m_wanderer.feet() - m_view.feet);
}

bool EntityDirector::stalkerBehindPlayer() const {
    const float d = stalkerDistance();
    if (d < 0.0f || d > 12.0f || m_stalker.seen()) return false;
    const glm::vec3 to = glm::normalize(m_stalker.feet() - m_view.feet + glm::vec3(0.0f, 1e-3f, 0.0f));
    return glm::dot(glm::vec2(to.x, to.z), glm::vec2(m_view.forward.x, m_view.forward.z)) < 0.3f;
}

// ---- The Tesla gun's targets ------------------------------------------------------------------

int EntityDirector::arcTarget(const glm::vec3& from, const glm::vec3& aim, float reach, float cosCone, glm::vec3& point) const {
    int best = -1;
    float bestScore = 1e9f;
    const Agent* agents[2] = {&m_stalker, &m_wanderer};
    for (int id = 0; id < 2; ++id) {
        const Agent& a = *agents[id];
        if (!a.active() || a.dying() || a.level() != m_view.level) continue;
        bool visible = false, tested = false;
        for (const CreatureRig::Limb& l : a.body().limbs()) {
            // The arc jumps to the part of the body closest to the line of fire.
            const glm::vec3 p = (l.a + l.b) * 0.5f;
            const glm::vec3 d = p - from;
            const float dist = glm::length(d);
            if (dist > reach + 0.3f || dist < 1e-3f) continue;
            const float facing = glm::dot(d / dist, aim);
            if (facing < cosCone) continue;
            const float score = (1.0f - facing) * 8.0f + dist * 0.15f;
            if (score >= bestScore) continue;
            if (!tested) { // walls and closed doors in between: once per body
                visible = m_nav.lineOfSight(a.level(), xz(from), xz(a.body().bounds().center()));
                tested = true;
            }
            if (!visible) break;
            bestScore = score;
            best = id;
            point = p;
        }
    }
    return best;
}

int EntityDirector::shockTest(const glm::vec3& a, const glm::vec3& b, float radius, glm::vec3& hit) const {
    int best = -1;
    float bestS = 2.0f;
    const Agent* agents[2] = {&m_stalker, &m_wanderer};
    const AABB segment = AABB(glm::min(a, b), glm::max(a, b)).expanded(radius);
    for (int id = 0; id < 2; ++id) {
        const Agent& agent = *agents[id];
        if (!agent.active() || agent.dying()) continue;
        const std::vector<CreatureRig::Limb>& limbs = agent.body().limbs();
        if (limbs.empty() || !segment.intersects(agent.body().bounds())) continue;
        // Segment against every limb capsule: the first contact along the bolt counts.
        for (const CreatureRig::Limb& l : limbs) {
            float s = 0.0f, t = 0.0f;
            const float r = radius + std::max(l.ra, l.rb);
            if (rig::segmentDistance2(a, b, l.a, l.b, s, t) > r * r || s >= bestS) continue;
            bestS = s;
            best = id;
            hit = a + (b - a) * s;
        }
    }
    return best;
}

void EntityDirector::applyShock(int target, float damage, const glm::vec3&) {
    if (target == 0) m_stalker.applyShock(damage);
    else if (target == 1) m_wanderer.applyShock(damage);
}

std::optional<EntityKind> EntityDirector::takeVaporised() {
    if (m_vaporised.empty()) return std::nullopt;
    const EntityKind k = m_vaporised.front();
    m_vaporised.erase(m_vaporised.begin());
    return k;
}
