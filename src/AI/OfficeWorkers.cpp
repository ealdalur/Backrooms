// ---------------------------------------------------------------------------
// OfficeWorkers.cpp
// ---------------------------------------------------------------------------
#include "AI/OfficeWorkers.h"

#include "Audio/StorySounds.h"
#include "Physics/Physics.h"
#include "Render/Camera.h"
#include "Render/EntityRenderer.h"
#include "World/OfficeLayout.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int   kMaxMotes = 60;
constexpr float kVoiceRange = 12.0f;  ///< The grumbles and snarls carry two or three cubicle pods...
constexpr float kTypingRange = 6.0f;  ///< ...the typing, about one.
constexpr float kScreamRange = 35.0f; ///< Shrieking under the arc carries further.

/// Body space: x to its left-to-right side, y up, z ahead (as in AI/Wanderer, AI/Stalker).
struct Frame {
    glm::vec3 origin, side, up, fwd;
    glm::vec3 operator()(const glm::vec3& l) const { return origin + side * l.x + up * l.y + fwd * l.z; }
    glm::vec3 dir(const glm::vec3& l) const { return side * l.x + up * l.y + fwd * l.z; }
    glm::vec3 local(const glm::vec3& w) const {
        const glm::vec3 d = w - origin;
        return {glm::dot(d, side), glm::dot(d, up), glm::dot(d, fwd)};
    }
};
Frame frameOf(const glm::vec3& feet, float yaw) {
    return {feet, glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw)), glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw))};
}

float ease(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

} // namespace

OfficeWorkers::OfficeWorkers(uint64_t seed, const ICollisionWorld& world, const Physics& physics)
    : m_world(world), m_physics(physics), m_rng(seed) {}

void OfficeWorkers::clockIn() {
    m_wanderer = Body{};
    m_stalker = Body{};
    m_motes.clear();
    m_vaporised.clear();
    place(m_wanderer, Wanderer);
    place(m_stalker, Stalker);
}

void OfficeWorkers::place(Body& b, Worker worker) {
    b.desk = office::workerDesk(worker);
    b.seat = glm::vec3(b.desk * glm::vec4(office::kWorkerSeat, 1.0f));
    b.keys = glm::vec3(b.desk * glm::vec4(office::kWorkerKeys, 1.0f));
    const glm::vec3 toDesk = b.keys - b.seat;
    b.facing = std::atan2(toDesk.x, toDesk.z);
    b.place(b.seat, office::kLevel, b.facing);
    if (worker == Stalker) b.setVulnerability(1.3f); // as in the Backrooms: the arc's light tears through a shadow
    b.time = m_rng.range(0.0f, 10.0f);
    b.typeTimer = m_rng.range(2.0f, 5.0f);
    b.voiceTimer = m_rng.range(3.0f, 8.0f);
    if (worker == Wanderer) poseWanderer(b);
    else poseStalker(b);
}

void OfficeWorkers::vitals(Body& b, Worker worker, float dt, std::vector<Sound>& sounds) {
    const Body::VitalSigns v = b.vitals(dt);
    const bool wanderer = worker == Wanderer;
    if (v.pain) sounds.push_back({wanderer ? SoundId::WandererPain : SoundId::StalkerPain, -1, b.head, 0.8f, 1.5f, kScreamRange});
    if (v.died) {
        sounds.push_back({wanderer ? SoundId::WandererDeath : SoundId::StalkerDeath, -1, b.head, 0.85f, 1.5f, kScreamRange});
        sounds.push_back({SoundId::Vaporize, -1, b.seat + glm::vec3(0.0f, 0.9f, 0.0f), 0.7f, 1.5f, kScreamRange});
        m_vaporised.push_back(worker);
    }
}

std::optional<OfficeWorkers::Worker> OfficeWorkers::takeVaporised() {
    if (m_vaporised.empty()) return std::nullopt;
    const Worker w = m_vaporised.front();
    m_vaporised.erase(m_vaporised.begin());
    return w;
}

// ---- Behaviour -------------------------------------------------------------------------------

void OfficeWorkers::update(float dt, std::vector<Sound>& sounds) {
    if (m_wanderer.active()) vitals(m_wanderer, Wanderer, dt, sounds);
    if (m_stalker.active()) vitals(m_stalker, Stalker, dt, sounds);

    if (m_wanderer.active()) {
        // Types in fits and starts; every so often sags back and complains.
        // (Under the arc, or burning away, it does neither.)
        Body& b = m_wanderer;
        b.time += dt;
        b.talking = b.atWork() ? std::max(0.0f, b.talking - dt) : 0.0f;
        if (b.atWork() && (b.voiceTimer -= dt) <= 0.0f) {
            const int count = storysfx::workGrumbleCount();
            int line = m_rng.rangeInt(0, count - 1);
            if (line == m_lastLine && count > 1) line = (line + 1) % count; // never the same gripe twice in a row
            m_lastLine = line;
            sounds.push_back({SoundId::WorkGrumble, line, b.head, 0.85f, 1.5f, kVoiceRange});
            b.talking = 2.6f;
            b.voiceTimer = m_rng.range(9.0f, 16.0f);
            m_stalker.voiceTimer = std::max(m_stalker.voiceTimer, 3.5f); // no snarling over the punchline
        }
        if ((b.typeTimer -= dt) <= 0.0f) {
            b.typing = !b.typing;
            b.typeTimer = b.typing ? m_rng.range(2.0f, 6.0f) : m_rng.range(0.8f, 2.5f);
        }
        b.slump += ((b.talking > 0.0f ? 1.0f : 0.0f) - b.slump) * ease(3.0f, dt);
        if (b.atWork() && b.typing && b.talking <= 0.0f) keystrokes(b, dt, 7.0f, 0.22f, sounds);
        poseWanderer(b);
        b.shudder();
    }
    if (m_stalker.active()) {
        // Hammers away at the keys, with the odd pause; snarls now and then.
        Body& b = m_stalker;
        b.time += dt;
        if (b.atWork() && (b.voiceTimer -= dt) <= 0.0f) {
            sounds.push_back({SoundId::WorkSnarl, -1, b.head, 0.7f, 1.5f, kVoiceRange});
            b.voiceTimer = m_rng.range(7.0f, 14.0f);
            m_wanderer.voiceTimer = std::max(m_wanderer.voiceTimer, 2.5f);
        }
        if ((b.typeTimer -= dt) <= 0.0f) {
            b.typing = !b.typing;
            b.typeTimer = b.typing ? m_rng.range(4.0f, 9.0f) : m_rng.range(0.4f, 1.2f);
        }
        if (b.atWork() && b.typing) keystrokes(b, dt, 11.0f, 0.25f, sounds);
        poseStalker(b);
        b.shudder();

        // Soot sheds from it as it works.
        const std::vector<CreatureRig::Limb>& limbs = b.rig.limbs();
        m_moteTimer -= dt;
        while (!b.dying() && !limbs.empty() && m_moteTimer <= 0.0f && static_cast<int>(m_motes.size()) < kMaxMotes) {
            m_moteTimer += 1.0f / 24.0f;
            const CreatureRig::Limb& l = limbs[m_rng.next() % limbs.size()];
            const float t = m_rng.nextFloat();
            const glm::vec3 jitter(m_rng.range(-1.0f, 1.0f), m_rng.range(-1.0f, 1.0f), m_rng.range(-1.0f, 1.0f));
            m_motes.push_back({glm::mix(l.a, l.b, t) + jitter * glm::mix(l.ra, l.rb, t),
                               glm::vec3(m_rng.range(-0.1f, 0.1f), m_rng.range(0.2f, 0.5f), m_rng.range(-0.1f, 0.1f)), 0.0f,
                               m_rng.range(1.0f, 2.0f), m_rng.range(0.03f, 0.08f)});
        }
        m_moteTimer = std::max(m_moteTimer, 0.0f);
    }
    // (What it has shed drifts off and fades, whatever became of it.)
    for (Mote& m : m_motes) {
        m.age += dt;
        m.pos += m.vel * dt;
        m.vel *= std::exp(-0.8f * dt);
    }
    m_motes.erase(std::remove_if(m_motes.begin(), m_motes.end(), [](const Mote& m) { return m.age >= m.life; }), m_motes.end());
}

void OfficeWorkers::keystrokes(Body& b, float dt, float rate, float gain, std::vector<Sound>& sounds) {
    if ((b.keyTimer -= dt) > 0.0f) return;
    b.keyTimer = m_rng.range(0.4f, 1.6f) / rate;
    sounds.push_back({SoundId::TerminalKey, -1, b.keys, gain * m_rng.range(0.7f, 1.0f), 1.0f, kTypingRange});
}

// ---- Poses ---------------------------------------------------------------------------------------

void OfficeWorkers::poseWanderer(Body& b) {
    // The Wanderer's starved, stooped body (AI/Wanderer), folded into a chair.
    const Frame F = frameOf(b.seat, b.facing);
    const float t = b.time, slump = b.slump;
    const bool typing = b.atWork() && b.typing && b.talking <= 0.0f;
    b.rig.clear();
    b.mouth.clear();

    // Sat well back in the chair, against its backrest.
    const glm::vec3 pelvis{0.0f, 0.60f, -0.13f};
    const glm::vec3 belly{0.0f, 0.86f, -0.07f - 0.04f * slump};
    const glm::vec3 chest{0.0f, 1.11f, 0.0f - 0.10f * slump};
    const glm::vec3 neck{0.0f, 1.31f, 0.11f - 0.16f * slump};
    b.rig.limb(F(pelvis), F(belly), 0.12f, 0.085f);
    b.rig.limb(F(belly), F(chest), 0.085f, 0.14f);
    b.rig.limb(F(chest), F(neck), 0.12f, 0.045f);

    // The long eyeless head hangs towards the screen it cannot see - or lolls back, complaining.
    const glm::vec3 headBase = neck + glm::vec3(0.0f, 0.06f, 0.03f);
    const float cock = 0.25f * std::sin(t * 0.45f) + (b.talking > 0.0f ? 0.2f * std::sin(t * 1.7f) : 0.0f);
    const glm::vec3 headDir = glm::normalize(glm::vec3(cock, 1.0f, 0.55f - 0.75f * slump));
    const glm::vec3 headTip = headBase + headDir * 0.42f;
    b.rig.limb(F(neck), F(headBase), 0.045f, 0.075f);
    b.rig.limb(F(headBase), F(headTip), 0.085f, 0.125f);
    b.head = F(glm::mix(headBase, headTip, 0.5f));
    const glm::vec3 faceDir = glm::normalize(glm::vec3(0.0f, -headDir.z, headDir.y));
    const glm::vec3 mouthTop = glm::mix(headBase, headTip, 0.42f) + faceDir * 0.1f;
    const float gape = b.talking > 0.0f ? 0.06f + 0.06f * std::fabs(std::sin(t * 11.0f)) : 0.05f + 0.02f * std::sin(t * 3.1f);
    b.mouth.limb(F(mouthTop), F(mouthTop - headDir * gape), 0.022f, 0.028f);

    const glm::vec3 keys = F.local(b.keys);
    for (float s : {-1.0f, 1.0f}) {
        // Arms: hands on the keyboard, fingers pecking - or resting on the desk.
        const glm::vec3 shoulder = chest + glm::vec3(0.20f * s, 0.12f, 0.0f);
        const float tap = typing ? 0.03f * std::pow(std::max(0.0f, std::sin(t * 12.0f + s * 1.9f + 1.5f * std::sin(t * 2.3f + s))), 3.0f) : 0.0f;
        const glm::vec3 hand = typing ? keys + glm::vec3(0.11f * s, 0.03f + tap, -0.02f) : keys + glm::vec3(0.2f * s, -0.01f, -0.12f);
        const rig::TwoBone arm = rig::solveTwoBone(F(shoulder), F(hand), 0.44f, 0.48f, F.dir(glm::normalize(glm::vec3(0.8f * s, -1.0f, -0.4f))));
        b.rig.limb(F(shoulder), arm.joint, 0.045f, 0.032f);
        b.rig.limb(arm.joint, arm.end, 0.032f, 0.024f);
        const glm::vec3 fore = glm::normalize(arm.end - arm.joint);
        for (int f = 0; f < 4; ++f) {
            const float spread = (static_cast<float>(f) - 1.5f) * 0.25f;
            const glm::vec3 d1 = glm::normalize(fore * 0.4f + F.fwd * 0.6f + F.side * spread - F.up * 0.35f);
            const glm::vec3 d2 = glm::normalize(d1 - F.up * 0.8f);
            const glm::vec3 k1 = arm.end + d1 * 0.1f;
            b.rig.limb(arm.end, k1, 0.012f, 0.009f);
            b.rig.limb(k1, k1 + d2 * 0.07f, 0.009f, 0.005f);
        }

        // Legs: knees under the desk, bare feet flat on the carpet.
        const glm::vec3 hip = pelvis + glm::vec3(0.11f * s, -0.03f, 0.0f);
        const glm::vec3 foot{0.17f * s, 0.03f, 0.44f};
        const rig::TwoBone leg = rig::solveTwoBone(F(hip), F(foot), 0.50f, 0.56f, F.dir(glm::normalize(glm::vec3(0.15f * s, 0.5f, 1.0f))));
        b.rig.limb(F(hip), leg.joint, 0.07f, 0.045f);
        b.rig.limb(leg.joint, leg.end, 0.045f, 0.03f);
        b.rig.limb(leg.end, leg.end + F.fwd * 0.17f - F.up * 0.02f, 0.035f, 0.02f);
    }
}

void OfficeWorkers::poseStalker(Body& b) {
    // The Stalker's spindly shadow (AI/Stalker), hunched over its keyboard,
    // elbows splayed like a spider's, claws tapping at the keys.
    const Frame F = frameOf(b.seat, b.facing);
    const float t = b.time;
    const bool typing = b.atWork() && b.typing;
    b.rig.clear();

    const glm::vec3 pelvis{0.0f, 0.62f, -0.14f};
    const glm::vec3 chest = glm::vec3(0.0f, 1.20f, 0.04f) + glm::vec3(0.0f, 0.012f * std::sin(t * 2.3f), 0.0f);
    const glm::vec3 neck{0.0f, 1.42f, 0.18f};
    const glm::vec3 waist = glm::mix(pelvis, chest, 0.5f) + glm::vec3(0.0f, 0.02f, -0.04f);
    const float twitch = 0.08f * std::sin(t * 17.0f) * std::max(0.0f, std::sin(t * 1.3f) - 0.8f) * 5.0f;
    const glm::vec3 headDir{0.38f + 0.2f * std::sin(t * 0.7f) + twitch, 1.0f, 0.55f};
    const glm::vec3 tip = neck + glm::normalize(headDir) * 0.30f;
    b.rig.limb(F(pelvis), F(waist), 0.12f, 0.085f);
    b.rig.limb(F(waist), F(chest), 0.085f, 0.15f);
    b.rig.limb(F(chest), F(neck), 0.13f, 0.045f);
    b.rig.limb(F(neck), F(tip), 0.055f, 0.105f);
    b.head = F(tip);

    const glm::vec3 keys = F.local(b.keys);
    for (float s : {-1.0f, 1.0f}) {
        const glm::vec3 shoulder{0.22f * s, 1.30f, 0.06f};
        const float tap = typing ? 0.035f * std::pow(std::max(0.0f, std::sin(t * 19.0f + s * 2.6f + std::sin(t * 5.3f))), 2.0f) : 0.0f;
        const glm::vec3 hand = keys + glm::vec3(0.10f * s, 0.13f + tap, -0.04f);
        const rig::TwoBone arm = rig::solveTwoBone(F(shoulder), F(hand), 0.62f, 0.70f, F.dir(glm::normalize(glm::vec3(1.0f * s, 0.9f, -0.3f))));
        b.rig.limb(F(shoulder), arm.joint, 0.05f, 0.035f);
        b.rig.limb(arm.joint, arm.end, 0.035f, 0.022f);
        for (float f : {-1.0f, 0.0f, 1.0f}) {
            const glm::vec3 d = glm::normalize(F.fwd * 0.45f - F.up + F.side * (0.3f * f * s));
            b.rig.limb(arm.end, arm.end + d * 0.16f, 0.014f, 0.004f);
        }

        const glm::vec3 hip = pelvis + glm::vec3(0.12f * s, -0.04f, 0.0f);
        const glm::vec3 foot{0.26f * s, 0.02f, 0.5f};
        const rig::TwoBone leg = rig::solveTwoBone(F(hip), F(foot), 0.56f, 0.62f, F.dir(glm::normalize(glm::vec3(0.6f * s, 0.8f, 0.5f))));
        b.rig.limb(F(hip), leg.joint, 0.075f, 0.05f);
        b.rig.limb(leg.joint, leg.end, 0.05f, 0.03f);
        b.rig.limb(leg.end, leg.end + F.dir(glm::normalize(glm::vec3(0.0f, -0.1f, 1.0f))) * 0.16f, 0.03f, 0.012f);
    }

    // Two pinprick eyes, on the screen.
    const glm::vec3 headAxis = glm::normalize(F(tip) - F(neck));
    const glm::vec3 face = glm::normalize(F.fwd - headAxis * glm::dot(F.fwd, headAxis) + 1e-4f * F.up);
    const glm::vec3 eyeCentre = glm::mix(F(neck), F(tip), 0.62f) + face * 0.1f;
    m_eyes[0] = eyeCentre + F.side * 0.042f;
    m_eyes[1] = eyeCentre - F.side * 0.042f;
}

void OfficeWorkers::buildDrawList(EntityDrawList& list, const Camera& camera) const {
    const glm::vec3 right = camera.right();
    const glm::vec3 up = glm::normalize(glm::cross(right, camera.forward()));
    if (m_wanderer.active()) {
        if (m_wanderer.dying()) {
            EntityDrawList::Dissolving& d = list.litDissolve;
            m_wanderer.rig.appendMesh(d.mesh, MaterialId::Flesh, 10);
            m_wanderer.mouth.appendMesh(d.mesh, MaterialId::DarkPlastic, 8);
            d.amount = m_wanderer.dissolve();
            d.feet = m_wanderer.seat;
            d.height = 1.9f; // sat down: the head is lower
            m_wanderer.embers(list, right, up);
        } else {
            m_wanderer.rig.appendMesh(list.lit, MaterialId::Flesh, 10);
            m_wanderer.mouth.appendMesh(list.lit, MaterialId::DarkPlastic, 8);
        }
    }
    if (m_stalker.active()) {
        MeshData& target = m_stalker.dying() ? list.shadowDissolve.mesh : list.shadow;
        const size_t first = target.vertices.size();
        m_stalker.rig.appendMesh(target, MaterialId::Wallpaper, 10);
        // The shadow shader reads the "material" slot as a sprite kind: 0 = body.
        for (size_t i = first; i < target.vertices.size(); ++i) target.vertices[i].material = 0.0f;
        if (m_stalker.dying()) {
            list.shadowDissolve.amount = m_stalker.dissolve();
            list.shadowDissolve.feet = m_stalker.seat;
            list.shadowDissolve.height = 1.9f;
            m_stalker.embers(list, right, up);
        }
        if (m_stalker.dissolve() < 0.25f) { // the eyes go out first
            for (const glm::vec3& e : m_eyes) list.addSprite(e, 0.011f, EntityDrawList::Eye, 1.0f, right, up);
        }
    }
    for (const Mote& m : m_motes) { // (soot already shed outlives the body)
        const float life = m.age / m.life;
        const float opacity = 0.8f * (1.0f - life) * std::min(1.0f, m.age * 6.0f);
        list.addSprite(m.pos, m.size * (1.0f + life), EntityDrawList::Smoke, opacity, right, up);
    }
}

// ---- The Tesla gun ---------------------------------------------------------------------------------

bool OfficeWorkers::clearLine(const glm::vec3& a, const glm::vec3& b) const {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-3f) return true;
    float t = 0.0f; // (stopping short of the body: its chair and desk are right there)
    return !m_physics.raycast(a, d / len, std::max(0.0f, len - 0.45f), m_world, t);
}

int OfficeWorkers::arcTarget(const glm::vec3& from, const glm::vec3& aim, float reach, float cosCone, glm::vec3& point) const {
    int best = -1;
    float bestScore = 1e9f;
    const Body* bodies[2] = {&m_wanderer, &m_stalker};
    for (int id = 0; id < 2; ++id) {
        const Body& b = *bodies[id];
        if (!b.active() || b.dying()) continue;
        bool visible = false, tested = false;
        for (const CreatureRig::Limb& l : b.rig.limbs()) {
            // The arc jumps to the part of the body closest to the line of fire.
            const glm::vec3 p = (l.a + l.b) * 0.5f;
            const glm::vec3 d = p - from;
            const float dist = glm::length(d);
            if (dist > reach + 0.3f || dist < 1e-3f) continue;
            const float facing = glm::dot(d / dist, aim);
            if (facing < cosCone) continue;
            const float score = (1.0f - facing) * 8.0f + dist * 0.15f;
            if (score >= bestScore) continue;
            if (!tested) { // cubicle walls in between: once per body (its head over them will do)
                visible = clearLine(from, b.rig.bounds().center()) || clearLine(from, b.head);
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

int OfficeWorkers::shockTest(const glm::vec3& a, const glm::vec3& b, float radius, glm::vec3& hit) const {
    int best = -1;
    float bestS = 2.0f;
    const Body* bodies[2] = {&m_wanderer, &m_stalker};
    const AABB segment = AABB(glm::min(a, b), glm::max(a, b)).expanded(radius);
    for (int id = 0; id < 2; ++id) {
        const Body& w = *bodies[id];
        if (!w.active() || w.dying()) continue;
        const std::vector<CreatureRig::Limb>& limbs = w.rig.limbs();
        if (limbs.empty() || !segment.intersects(w.rig.bounds())) continue;
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

void OfficeWorkers::applyShock(int target, float damage, const glm::vec3&) {
    if (target == Wanderer) m_wanderer.applyShock(damage);
    else if (target == Stalker) m_stalker.applyShock(damage);
}
