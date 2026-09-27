// ---------------------------------------------------------------------------
// Stalker.cpp
// ---------------------------------------------------------------------------
#include "AI/Stalker.h"

#include "Core/Config.h"
#include "Render/EntityRenderer.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kHalfWidth = 0.28f;
constexpr float kHeight = 1.9f;     ///< Collision height (it stoops under the ceiling).
constexpr int   kMaxMotes = 70;

inline glm::vec2 xz(const glm::vec3& v) { return {v.x, v.z}; }

/// Local body frame: +z forward (yaw), +y up, +x to the side.
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

Stalker::Stalker(uint64_t seed) : Agent(kHalfWidth, kHeight), m_rng(seed) {}

void Stalker::place(const glm::vec3& feet, int level, float yaw) {
    Agent::place(feet, level, yaw);
    m_state = State::Lurking;
    m_timer = m_rng.range(3.0f, 9.0f);
    m_exposure = 0.0f;
    m_coverTries = 0;
    m_flees = 0;
    m_patient = false;
    m_crawl = 0.0f;
    m_stride = 0.0f;
    m_motes.clear();
    pose();
}

void Stalker::provoke() {
    if (m_state != State::Lurking) return;
    m_state = State::Stalking;
    m_replan = 0.0f;
}

glm::vec3 Stalker::confront(const glm::vec3& playerFeet, const glm::vec3& playerForward) {
    const glm::vec3 fwd = glm::normalize(glm::vec3(playerForward.x, 0.0f, playerForward.z) + glm::vec3(1e-4f, 0.0f, 0.0f));
    m_feet = glm::vec3(playerFeet.x, m_feet.y, playerFeet.z) + fwd * 0.75f;
    m_yaw = std::atan2(-fwd.x, -fwd.z); // facing the player
    m_velocity = glm::vec3(0.0f);
    m_state = State::Frozen;
    m_crawl = 0.0f;
    m_stride = 0.0f;
    pose();
    updateMotes(0.0f);
    return (m_eyes[0] + m_eyes[1]) * 0.5f;
}

bool Stalker::moving() const {
    return (m_state == State::Stalking || m_state == State::Fleeing || m_state == State::Lunging) &&
           glm::length(xz(m_velocity)) > 2.5f;
}

LightDisturbance Stalker::lightDisturbance() const {
    return {m_feet + glm::vec3(0.0f, 1.3f, 0.0f), cfg::kStalkerDrainRadius, moving() ? 0.95f : 0.6f};
}

// ---- Perception -------------------------------------------------------------------------

bool Stalker::isSeenBy(const PlayerView& player, const NavGrid& nav) const {
    if (player.viewBlocked || player.level != m_level) return false;
    // Frustum culling as a sense: is any part of the body inside the view volume?
    const AABB box = m_rig.limbs().empty() ? bodyBox() : m_rig.bounds();
    if (!player.frustum.isVisible(box)) return false;
    const float dist = glm::length(box.center() - player.eye);
    if (dist > 45.0f) return false;
    // A black shape in a dark room: invisible unless it is close.
    if (dist > cfg::kStalkerDarkSightDist && nav.cellBrightness(m_level, NavGrid::cellOf(m_feet)) < cfg::kStalkerDarkLevel) {
        return false;
    }
    const glm::vec3 samples[3] = {m_eyes[0], box.center(), m_feet + glm::vec3(0.0f, 0.3f, 0.0f)};
    for (const glm::vec3& p : samples) {
        if (nav.lineOfSight(m_level, xz(player.eye), xz(p))) return true;
    }
    return false;
}

NavProfile Stalker::huntProfile(const PlayerView& player, const NavGrid& nav) const {
    NavProfile p;
    p.darkPreference = 3.0f; // lit cells cost up to ~4x a dark one
    p.passDoors = true;      // it slips under them
    // Cells the player is looking at are expensive: it circles round and comes from behind.
    const int level = m_level;
    p.cellCost = [player, level, &nav](const glm::ivec2& cell) {
        const glm::vec3 c = NavGrid::cellCenter(cell, level) + glm::vec3(0.0f, 1.2f, 0.0f);
        if (glm::length(c - player.eye) > 28.0f) return 0.0f;
        if (!player.frustum.isVisible(AABB::fromCenterHalf(c, glm::vec3(0.4f)))) return 0.0f;
        return nav.lineOfSight(level, xz(player.eye), xz(c)) ? 6.0f : 0.0f;
    };
    return p;
}

bool Stalker::findCover(const PlayerView& player, const NavGrid& nav, glm::vec3& out) const {
    // Breadth-first over nearby cells: the closest spot the player cannot see.
    struct Item {
        glm::ivec2 cell;
        int        depth;
    };
    std::vector<Item> queue{{NavGrid::cellOf(m_feet), 0}};
    std::vector<glm::ivec2> visited{queue[0].cell};
    NavProfile profile;
    const float y = world::levelFloorY(m_level);
    const glm::vec2 offsets[5] = {{0.0f, 0.0f}, {1.5f, 1.5f}, {-1.5f, 1.5f}, {1.5f, -1.5f}, {-1.5f, -1.5f}};
    float bestScore = 1e9f;
    for (size_t head = 0; head < queue.size(); ++head) {
        const Item it = queue[head];
        const glm::vec3 centre = NavGrid::cellCenter(it.cell, m_level);
        for (const glm::vec2& o : offsets) {
            const glm::vec3 p(centre.x + o.x, y, centre.z + o.y);
            const float toPlayer = glm::length(xz(p) - xz(player.feet));
            if (toPlayer < 3.5f) continue;
            const glm::vec2 line = xz(p) - xz(player.eye);
            const glm::vec2 perp = glm::normalize(glm::vec2(-line.y, line.x)) * 0.35f;
            const bool hidden = !nav.lineOfSight(m_level, xz(player.eye), xz(p)) &&
                                !nav.lineOfSight(m_level, xz(player.eye), xz(p) + perp) &&
                                !nav.lineOfSight(m_level, xz(player.eye), xz(p) - perp);
            if (!hidden) continue;
            const float score = static_cast<float>(it.depth) * 5.0f + nav.cellBrightness(m_level, it.cell) * 3.0f +
                                std::max(0.0f, 12.0f - toPlayer) * 0.4f;
            if (score < bestScore) {
                bestScore = score;
                out = p;
            }
        }
        if (it.depth >= 3) continue;
        const glm::ivec2 steps[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const glm::ivec2& s : steps) {
            const glm::ivec2 nb = it.cell + s;
            if (std::find(visited.begin(), visited.end(), nb) != visited.end()) continue;
            if (!nav.canStep(m_level, it.cell, nb, profile)) continue;
            visited.push_back(nb);
            queue.push_back({nb, it.depth + 1});
        }
    }
    return bestScore < 1e8f;
}

// ---- Behaviour ------------------------------------------------------------------------------

void Stalker::freeze() {
    m_state = State::Frozen;
    m_velocity.x = m_velocity.z = 0.0f; // stops dead, mid-stride
    m_timer = 0.0f;
}

void Stalker::startFleeing(const PlayerView& player, const NavGrid& nav, std::vector<EntitySound>& sounds) {
    glm::vec3 cover;
    if (findCover(player, nav, cover) && planTo(nav, cover, NavProfile{})) {
        m_state = State::Fleeing;
        ++m_coverTries;
        sounds.push_back({EntitySound::Type::StalkerHiss, m_feet + glm::vec3(0.0f, 1.0f, 0.0f), 1.0f});
    } else {
        m_exposure = 0.0f; // nowhere to go: it holds its ground and waits
    }
}

bool Stalker::update(float dt, const PlayerView& player, const NavGrid& nav, const ICollisionWorld& world,
                     const Physics& physics, std::vector<EntitySound>& sounds) {
    if (!m_active) return false;
    m_seen = isSeenBy(player, nav);
    const glm::vec3 toPlayer = player.feet - m_feet;
    const float dist = glm::length(xz(toPlayer));
    const bool sameLevel = player.level == m_level;
    bool caught = false;

    switch (m_state) {
    case State::Lurking:
        integrate(dt, glm::vec3(0.0f), 10.0f, world, physics);
        turnTowards(toPlayer, 1.2f, dt);
        if (m_seen) {
            m_exposure += dt;
            if (m_exposure > 0.7f) startFleeing(player, nav, sounds);
        } else {
            m_exposure = std::max(0.0f, m_exposure - 0.5f * dt);
            m_timer -= dt;
            // Freshly arrived it pounces on a player who wanders close; driven
            // into cover, it lies low for the full wait.
            if (m_timer <= 0.0f || (!m_patient && sameLevel && dist < 9.0f)) {
                m_state = State::Stalking;
                m_replan = 0.0f;
                m_patient = false;
            }
        }
        break;

    case State::Stalking:
        if (m_seen) {
            freeze();
            break;
        }
        m_replan -= dt;
        if (m_replan <= 0.0f) {
            planTo(nav, player.feet, huntProfile(player, nav));
            m_replan = 0.4f;
        }
        {
            // Rushes while far off, then creeps the last few metres before the lunge.
            const float prowl = std::clamp((dist - 3.0f) / (cfg::kStalkerProwlDist - 3.0f), 0.0f, 1.0f);
            followPath(dt, glm::mix(cfg::kStalkerCreepSpeed, cfg::kStalkerSpeed, prowl), 9.0f, nav, world, physics);
        }
        if (sameLevel && dist < cfg::kStalkerLungeRange && nav.lineOfSight(m_level, xz(m_feet), xz(player.feet))) {
            m_state = State::Lunging;
        }
        break;

    case State::Frozen:
        integrate(dt, glm::vec3(0.0f), 1000.0f, world, physics);
        if (m_seen) {
            // Being stared at is intolerable - the closer, the worse.
            m_exposure += dt * (1.0f + std::max(0.0f, 6.0f - dist) * 0.5f);
            if (m_exposure > cfg::kStalkerExposureLimit) startFleeing(player, nav, sounds);
        } else {
            m_timer += dt;
            if (m_timer > 0.12f) { // the moment you look away...
                m_state = State::Stalking;
                m_replan = 0.0f;
            }
        }
        break;

    case State::Fleeing: {
        // The dart happens even while watched: a blur round the corner.
        const bool arrived = followPath(dt, cfg::kStalkerDartSpeed, 16.0f, nav, world, physics);
        if (arrived || m_stuck > 0.6f) {
            if (m_seen && m_coverTries < 3) {
                startFleeing(player, nav, sounds);
            } else {
                m_state = State::Lurking;
                m_timer = m_rng.range(4.0f, 9.0f);
                m_exposure = 0.0f;
                m_coverTries = 0;
                m_patient = true;
                ++m_flees;
            }
        }
        break;
    }

    case State::Lunging: {
        const glm::vec3 dir = dist > 1e-3f ? glm::vec3(toPlayer.x, 0.0f, toPlayer.z) / dist : forward();
        integrate(dt, dir * cfg::kStalkerLungeSpeed, 14.0f, world, physics);
        turnTowards(dir, 12.0f, dt);
        if (m_seen && dist > 1.1f) {
            freeze(); // turned round just in time
        } else if (sameLevel && dist < cfg::kCatchDistance && std::fabs(toPlayer.y) < 1.2f) {
            caught = true;
        } else if (dist > cfg::kStalkerLungeRange * 2.0f) {
            m_state = State::Stalking;
        }
        break;
    }
    }

    if (moving()) {
        m_skitter -= dt;
        if (m_skitter <= 0.0f) {
            sounds.push_back({EntitySound::Type::StalkerSkitter, m_feet, std::min(1.0f, glm::length(xz(m_velocity)) / cfg::kStalkerSpeed)});
            m_skitter = m_rng.range(0.3f, 0.65f);
        }
    }

    animate(dt, player);
    updateMotes(dt);
    return caught;
}

// ---- Animation -------------------------------------------------------------------------------

void Stalker::animate(float dt, const PlayerView& player) {
    if (m_state == State::Frozen) return; // not a twitch while it is watched
    m_animTime += dt;
    const float speed = glm::length(xz(m_velocity));
    m_stride += (std::min(speed / cfg::kStalkerSpeed, 1.0f) * 0.42f - m_stride) * (1.0f - std::exp(-6.0f * dt));
    m_phase = std::fmod(m_phase + dt * (1.0f + speed * 3.4f), 2.0f * kPi);
    const bool onAllFours = m_state == State::Stalking || m_state == State::Fleeing || m_state == State::Lunging;
    m_crawl += ((onAllFours ? 1.0f : 0.0f) - m_crawl) * (1.0f - std::exp(-5.0f * dt));
    const glm::vec3 look = player.eye - (m_feet + glm::vec3(0.0f, 2.0f, 0.0f));
    if (glm::length(look) > 1e-3f) m_lookDir = glm::normalize(look);
    pose();
}

void Stalker::pose() {
    const Frame F = frameOf(m_feet, m_yaw);
    const float c = m_crawl, t = m_animTime;
    auto blend = [c](const glm::vec3& upright, const glm::vec3& crawling) { return glm::mix(upright, crawling, c); };
    m_rig.clear();

    // ---- Spine and head: hunched and impossibly tall, or low and long.
    const glm::vec3 pelvis = blend({0.0f, 1.12f, -0.02f}, {0.0f, 0.82f, -0.50f});
    const glm::vec3 chest  = blend({0.0f, 1.72f, 0.10f}, {0.0f, 0.92f, 0.32f}) + glm::vec3(0.0f, 0.012f * std::sin(t * 2.3f), 0.0f);
    const glm::vec3 neck   = blend({0.0f, 1.96f, 0.20f}, {0.0f, 0.93f, 0.60f});
    const glm::vec3 waist  = glm::mix(pelvis, chest, 0.5f) + glm::vec3(0.0f, 0.02f, -0.04f);
    // Upright, the head hangs cocked to one side and twitches; crawling it juts forward.
    const float twitch = 0.08f * std::sin(t * 17.0f) * std::max(0.0f, std::sin(t * 1.3f) - 0.8f) * 5.0f;
    glm::vec3 headDir = blend({0.38f + 0.2f * std::sin(t * 0.7f) + twitch, 1.0f, 0.42f}, {0.05f * std::sin(t * 0.9f), -0.15f, 1.0f});
    const glm::vec3 tip = neck + glm::normalize(headDir) * 0.30f;

    m_rig.limb(F(pelvis), F(waist), 0.12f, 0.085f);
    m_rig.limb(F(waist), F(chest), 0.085f, 0.15f);
    m_rig.limb(F(chest), F(neck), 0.13f, 0.045f);
    m_rig.limb(F(neck), F(tip), 0.055f, 0.105f);

    const float strideN = m_stride / 0.42f;
    for (float s : {-1.0f, 1.0f}) {
        // ---- Arms: hanging to the knees, or front legs with elbows like a spider's.
        const glm::vec3 shoulder = blend({0.22f * s, 1.84f, 0.12f}, {0.24f * s, 0.97f, 0.32f});
        const float ap = m_phase + (s > 0.0f ? 0.0f : kPi);
        const glm::vec3 handUp{0.34f * s, 0.55f + 0.03f * std::sin(t * 1.7f + s), 0.28f + 0.1f * m_stride * std::sin(ap)};
        const glm::vec3 handCrawl{0.40f * s, 0.02f + std::max(0.0f, std::cos(ap)) * 0.16f * strideN, 0.80f + m_stride * std::sin(ap)};
        const glm::vec3 pole = glm::normalize(blend({0.3f * s, 0.0f, -1.0f}, {1.0f * s, 1.1f, -0.3f}));
        const rig::TwoBone arm = rig::solveTwoBone(F(shoulder), F(blend(handUp, handCrawl)), 0.62f, 0.70f, F.dir(pole));
        m_rig.limb(F(shoulder), arm.joint, 0.05f, 0.035f);
        m_rig.limb(arm.joint, arm.end, 0.035f, 0.022f);
        const glm::vec3 fingerDir = glm::normalize(arm.end - arm.joint);
        for (float f : {-1.0f, 0.0f, 1.0f}) {
            const glm::vec3 d = glm::normalize(fingerDir + F.side * (0.35f * f * s) - F.up * 0.15f);
            m_rig.limb(arm.end, arm.end + d * 0.22f, 0.014f, 0.004f);
        }

        // ---- Legs: trotting diagonally opposite to the arms.
        const glm::vec3 hip = pelvis + glm::vec3(0.12f * s, -0.04f, 0.0f);
        const float lp = m_phase + (s > 0.0f ? kPi : 0.0f);
        const glm::vec3 footUp{0.16f * s, 0.03f, 0.05f * std::sin(lp) * strideN};
        const glm::vec3 footCrawl{0.30f * s, 0.02f + std::max(0.0f, std::cos(lp)) * 0.14f * strideN, -0.92f + m_stride * std::sin(lp)};
        const glm::vec3 knee = glm::normalize(blend({0.0f, 0.0f, 1.0f}, {0.8f * s, 0.9f, 0.3f}));
        const rig::TwoBone leg = rig::solveTwoBone(F(hip), F(blend(footUp, footCrawl)), 0.56f, 0.62f, F.dir(knee));
        m_rig.limb(F(hip), leg.joint, 0.075f, 0.05f);
        m_rig.limb(leg.joint, leg.end, 0.05f, 0.03f);
        m_rig.limb(leg.end, leg.end + F.dir(glm::normalize(blend({0.0f, -0.1f, 1.0f}, {0.0f, -0.3f, -1.0f}))) * 0.16f, 0.03f, 0.012f);
    }

    // ---- Eyes: two pinpricks on the face.
    const glm::vec3 headAxis = glm::normalize(F(tip) - F(neck));
    const glm::vec3 face = glm::normalize(F.fwd - headAxis * (glm::dot(F.fwd, headAxis) * (1.0f - c)) + 1e-4f * F.up);
    const glm::vec3 eyeCentre = glm::mix(F(neck), F(tip), glm::mix(0.62f, 0.85f, c)) + face * 0.1f;
    m_eyes[0] = eyeCentre + F.side * 0.042f;
    m_eyes[1] = eyeCentre - F.side * 0.042f;
}

void Stalker::updateMotes(float dt) {
    for (Mote& m : m_motes) {
        m.age += dt;
        m.pos += m.vel * dt;
        m.vel *= std::exp(-0.8f * dt);
    }
    m_motes.erase(std::remove_if(m_motes.begin(), m_motes.end(), [](const Mote& m) { return m.age >= m.life; }),
                  m_motes.end());

    // Soot constantly sheds from the body, even when it is frozen.
    const std::vector<CreatureRig::Limb>& limbs = m_rig.limbs();
    if (limbs.empty()) return;
    m_moteTimer -= dt;
    while (m_moteTimer <= 0.0f && static_cast<int>(m_motes.size()) < kMaxMotes) {
        m_moteTimer += 1.0f / 28.0f;
        const CreatureRig::Limb& l = limbs[m_rng.next() % limbs.size()];
        const float t = m_rng.nextFloat();
        const glm::vec3 jitter(m_rng.range(-1.0f, 1.0f), m_rng.range(-1.0f, 1.0f), m_rng.range(-1.0f, 1.0f));
        Mote m;
        m.pos = glm::mix(l.a, l.b, t) + jitter * glm::mix(l.ra, l.rb, t);
        m.vel = glm::vec3(m_rng.range(-0.1f, 0.1f), m_rng.range(0.2f, 0.5f), m_rng.range(-0.1f, 0.1f)) + m_velocity * 0.15f;
        m.age = 0.0f;
        m.life = m_rng.range(1.0f, 2.0f);
        m.size = m_rng.range(0.03f, 0.08f);
        m_motes.push_back(m);
    }
    if (m_moteTimer < 0.0f) m_moteTimer = 0.0f;
}

void Stalker::buildGeometry(EntityDrawList& list, const glm::vec3& camRight, const glm::vec3& camUp) const {
    if (!m_active) return;
    const size_t first = list.shadow.vertices.size();
    m_rig.appendMesh(list.shadow, MaterialId::Wallpaper, 10);
    // The shadow shader reads the "material" slot as a sprite kind: 0 = body.
    for (size_t i = first; i < list.shadow.vertices.size(); ++i) list.shadow.vertices[i].material = 0.0f;

    for (const Mote& m : m_motes) {
        const float life = m.age / m.life;
        const float opacity = 0.8f * (1.0f - life) * std::min(1.0f, m.age * 6.0f);
        list.addSprite(m.pos, m.size * (1.0f + life), EntityDrawList::Smoke, opacity, camRight, camUp);
    }
    for (const glm::vec3& e : m_eyes) list.addSprite(e, 0.011f, EntityDrawList::Eye, 1.0f, camRight, camUp);
}
