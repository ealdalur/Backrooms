// ---------------------------------------------------------------------------
// TeslaGun.cpp
// ---------------------------------------------------------------------------
#include "Gameplay/TeslaGun.h"

#include "Core/Config.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Shockable.h"
#include "Physics/Physics.h"

#include <algorithm>
#include <cmath>

namespace {
const glm::vec3 kArcColor(0.55f, 0.66f, 1.0f);   ///< Violet-blue glow round a white core.
const glm::vec3 kSparkColor(1.0f, 0.68f, 0.32f); ///< Molten metal.
constexpr size_t kMaxSparks = 160;
} // namespace

TeslaGun::TeslaGun(uint64_t seed) : m_rng(seed) {}

std::vector<GunSoundEvent> TeslaGun::takeSounds() {
    std::vector<GunSoundEvent> out;
    out.swap(m_sounds);
    return out;
}

void TeslaGun::spray(const glm::vec3& at, const glm::vec3& normal, int count) {
    for (int i = 0; i < count && m_sparks.size() < kMaxSparks; ++i) {
        const glm::vec3 random = lightning::coneDirection(glm::vec3(0.0f, 1.0f, 0.0f), 3.14159f, m_rng);
        Spark s;
        s.pos = at;
        s.vel = glm::normalize(normal * 0.7f + random) * m_rng.range(1.2f, 4.5f);
        s.age = 0.0f;
        s.life = m_rng.range(0.18f, 0.55f);
        m_sparks.push_back(s);
    }
}

void TeslaGun::strikeSet(const GunContext& ctx) {
    const float p = m_power;
    // A nearly flat battery sputters: many sets never break out at all.
    if (p < 0.06f && m_rng.chance(0.55f)) {
        m_connected = -1;
        return;
    }
    const float reach = glm::mix(cfg::kArcReachMin, cfg::kArcReachMax, std::pow(p, 0.8f));
    m_flicker = m_rng.range(0.55f, 1.0f);
    // Streamers fan out round the line to what the crosshair is on.
    const glm::vec3 aimDir = glm::normalize(ctx.eye + ctx.aim * reach - ctx.muzzle);

    lightning::StrikeParams base;
    base.width = glm::mix(0.006f, 0.016f, p);
    base.intensity = glm::mix(0.5f, 1.3f, p) * m_flicker;
    base.life = m_rng.range(0.03f, 0.07f);
    base.roughness = 0.24f;
    base.levels = 5;
    base.forkChance = glm::mix(0.05f, 0.12f, p);
    base.forkDepth = 2;
    base.color = kArcColor;

    const size_t first = m_bolts.size();
    glm::vec3 endSum(0.0f);
    int ends = 0;
    auto blocked = [&](const glm::vec3& dir, float length, float& t) {
        return ctx.physics && ctx.world && ctx.physics->raycast(ctx.muzzle, dir, length, *ctx.world, t);
    };

    // ---- The heavy arc: straight onto a body in reach (unless something stands in the way).
    glm::vec3 point;
    const int target = ctx.targets ? ctx.targets->arcTarget(ctx.muzzle, aimDir, reach, std::cos(glm::radians(cfg::kArcConeDeg)), point)
                                   : -1;
    if (target >= 0) {
        const glm::vec3 toPoint = point - ctx.muzzle;
        const float dist = glm::length(toPoint);
        const glm::vec3 dir = toPoint / std::max(dist, 1e-4f);
        float t = 0.0f;
        const bool wall = blocked(dir, std::max(0.0f, dist - 0.15f), t);
        const glm::vec3 end = wall ? ctx.muzzle + dir * t : point;
        lightning::StrikeParams heavy = base;
        heavy.width *= 1.4f;
        heavy.intensity *= 1.3f;
        heavy.roughness = 0.17f;
        heavy.forkChance *= 1.2f;
        const int arcs = 1 + ((p > 0.5f && m_rng.chance(0.5f)) ? 1 : 0);
        for (int i = 0; i < arcs; ++i) {
            const glm::vec3 jitter(m_rng.range(-0.05f, 0.05f), m_rng.range(-0.05f, 0.05f), m_rng.range(-0.05f, 0.05f));
            lightning::strike(m_bolts, ctx.muzzle, end + (wall ? glm::vec3(0.0f) : jitter), heavy, m_rng);
        }
        if (wall) spray(end, -dir, 4);
        endSum += end;
        ++ends;
    }

    // ---- Streamers breaking out into the air, or grounding on whatever they reach.
    // With an arc on a body most of the power goes into it: fewer, shorter streamers.
    const int count = (target >= 0 ? 1 : 2) + static_cast<int>(p * (target >= 0 ? 2.0f : 3.5f) + m_rng.nextFloat());
    const float spread = glm::radians(glm::mix(38.0f, 24.0f, p));
    for (int i = 0; i < count; ++i) {
        const glm::vec3 dir = lightning::coneDirection(aimDir, spread, m_rng);
        const float length = reach * m_rng.range(0.3f, 0.95f) * (target >= 0 ? 0.55f : 1.0f);
        float t = 0.0f;
        glm::vec3 end = ctx.muzzle + dir * length;
        if (blocked(dir, length, t)) {
            end = ctx.muzzle + dir * t;
            spray(end, -dir, 3);
        }
        lightning::StrikeParams air = base;
        air.width *= 0.8f;
        air.intensity *= 0.7f;
        air.roughness = 0.3f;
        air.forkChance *= 1.5f;
        lightning::strike(m_bolts, ctx.muzzle, end, air, m_rng);
        endSum += end;
        ++ends;
    }

    // ---- Collision: every segment of every new channel against the bodies.
    //      A channel that passes through one ends in it, and connects it.
    int connected = -1;
    if (ctx.targets) {
        for (size_t b = first; b < m_bolts.size(); ++b) {
            Bolt& bolt = m_bolts[b];
            for (size_t i = 0; i + 1 < bolt.points.size(); ++i) {
                glm::vec3 hit;
                const int id = ctx.targets->shockTest(bolt.points[i], bolt.points[i + 1], bolt.width * 3.0f, hit);
                if (id < 0) continue;
                connected = id;
                m_hitPoint = hit;
                bolt.points.resize(i + 2);
                bolt.points.back() = hit;
                break;
            }
        }
    }
    m_connected = connected;
    if (connected >= 0) spray(m_hitPoint, -aimDir, 2);
    m_arcEnd = ends > 0 ? endSum / static_cast<float>(ends) : ctx.muzzle + aimDir * reach * 0.5f;
}

void TeslaGun::update(float dt, const GunContext& ctx, Inventory& inventory) {
    m_noise = 0.0f;
    for (Bolt& b : m_bolts) b.age += dt;
    m_bolts.erase(std::remove_if(m_bolts.begin(), m_bolts.end(), [](const Bolt& b) { return b.age >= b.life; }), m_bolts.end());
    for (Spark& s : m_sparks) {
        s.age += dt;
        s.vel.y -= 9.8f * dt;
        s.vel *= std::exp(-1.5f * dt);
        s.pos += s.vel * dt;
    }
    m_sparks.erase(std::remove_if(m_sparks.begin(), m_sparks.end(), [](const Spark& s) { return s.age >= s.life; }),
                   m_sparks.end());
    m_glows.clear();
    m_muzzleGlow.intensity = 0.0f;

    const float charge = inventory.charge();
    const bool was = m_discharging;
    m_discharging = ctx.ready && ctx.trigger && charge > 0.0f;
    if (ctx.ready && ctx.triggerPressed && charge <= 0.0f) {
        m_sounds.push_back({GunSoundEvent::Type::DryClick, ctx.muzzle, 1.0f});
    }

    if (m_discharging) {
        m_power = charge;
        if (!was) {
            m_sounds.push_back({GunSoundEvent::Type::Zap, ctx.muzzle, 0.6f + 0.4f * m_power});
            m_strikeTimer = 0.0f;
            m_noiseTimer = 0.0f;
        }
        inventory.drain(dt / cfg::kBatteryLife);
        if (inventory.charge() <= 0.0f) m_sounds.push_back({GunSoundEvent::Type::Depleted, ctx.muzzle, 1.0f});

        m_strikeTimer -= dt;
        for (int guard = 0; m_strikeTimer <= 0.0f && guard < 3; ++guard) {
            strikeSet(ctx);
            m_strikeTimer += 1.0f / (cfg::kArcStrikeRate * m_rng.range(0.75f, 1.25f));
        }
        m_strikeTimer = std::max(m_strikeTimer, 0.0f);

        // A connected body burns for as long as the arc holds.
        if (m_connected >= 0 && ctx.targets) {
            ctx.targets->applyShock(m_connected, cfg::kShockDps * glm::mix(0.3f, 1.0f, m_power) * dt, m_hitPoint);
            if ((m_hitSoundTimer -= dt) <= 0.0f) {
                m_sounds.push_back({GunSoundEvent::Type::Hit, m_hitPoint, 0.6f + 0.4f * m_power});
                m_hitSoundTimer = m_rng.range(0.12f, 0.3f);
            }
            m_glows.push_back({m_hitPoint, 0.08f + 0.1f * m_power, 0.5f * m_flicker, kArcColor});
        }
        if ((m_noiseTimer -= dt) <= 0.0f) {
            m_noise = cfg::kNoiseTesla * (0.5f + 0.5f * m_power);
            m_noiseTimer = 0.5f;
        }
        // The corona round the spike, and the light the whole discharge throws.
        m_muzzleGlow = {ctx.muzzle, 0.03f + 0.03f * m_power * m_flicker, (0.5f + 0.7f * m_power) * m_flicker, kArcColor};
        const float reach = glm::mix(cfg::kArcReachMin, cfg::kArcReachMax, std::pow(m_power, 0.8f));
        m_light.position = ctx.muzzle + (m_arcEnd - ctx.muzzle) * 0.4f;
        m_light.color = kArcColor;
        m_light.intensity = cfg::kArcLightPower * (0.35f + 0.65f * m_power) * m_flicker;
        m_light.range = reach + 2.5f;
    } else {
        m_connected = -1;
        m_light.intensity = 0.0f;
    }

    // Low-battery warning: once on crossing the threshold, then on repeat (faster when nearly flat).
    const float now = inventory.charge();
    if (ctx.ready && now > 0.0f && now < cfg::kLowBattery) {
        m_warnTimer -= dt;
        if (!m_lowWarned || m_warnTimer <= 0.0f) {
            m_sounds.push_back({GunSoundEvent::Type::LowBattery, ctx.muzzle, 1.0f});
            m_warnTimer = now < 0.08f ? 1.0f : 2.4f;
            m_lowWarned = true;
        }
    } else if (now >= cfg::kLowBattery) {
        m_lowWarned = false;
    }

    // ---- What gets drawn: the channels, and a short streak behind every spark.
    m_draw = m_bolts;
    for (const Spark& s : m_sparks) {
        Bolt streak;
        streak.points = {s.pos, s.pos - s.vel * 0.025f};
        streak.width = 0.0035f;
        streak.taper = 1.0f;
        streak.intensity = 4.0f * (1.0f - s.age / s.life);
        streak.color = kSparkColor;
        streak.age = 0.0f;
        streak.life = 1.0f;
        m_draw.push_back(std::move(streak));
    }
}
