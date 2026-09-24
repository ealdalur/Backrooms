// ---------------------------------------------------------------------------
// Soundscape.cpp
// ---------------------------------------------------------------------------
#include "Audio/Soundscape.h"

#include "Actors/Door.h"
#include "Actors/Player.h"
#include "World/ChunkManager.h"
#include "World/WorldGenerator.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
// ---- Mix levels (linear gain; every source is peak-normalised) ----------------
constexpr float kHumFloor      = 0.02f;  ///< Hum that never fully disappears.
constexpr float kHumPerLight   = 0.035f;
constexpr float kHumMax        = 0.12f;
constexpr float kBuzzPerLight  = 0.12f;
constexpr float kBuzzMax       = 0.22f;
constexpr float kDroneGain     = 0.09f;
constexpr float kFootBase      = 0.18f;
constexpr float kFootPerSpeed  = 0.30f;
constexpr float kLandBase      = 0.35f;
constexpr float kLandPerImpact = 0.50f;
constexpr float kGruntGain     = 0.30f;
constexpr float kUnlatchGain   = 0.55f;
constexpr float kCreakGain     = 0.20f;  ///< Creaks are meant to be subtle.
constexpr float kShutGain      = 0.65f;
constexpr float kStrikeGain    = 0.35f;
constexpr float kDropGain      = 0.20f;

// ---- Behaviour -------------------------------------------------------------------
constexpr float  kLightHearingRange = 14.0f; ///< Lights farther away are inaudible.
constexpr float  kFlickerThreshold  = 0.5f;  ///< On/off boundary for click detection.
constexpr double kMinClickInterval  = 0.06;  ///< Per-light rate limit (s).
constexpr double kStutterWindow     = 0.25;  ///< Clicks closer together than this are "stutter"...
constexpr float  kStutterGain       = 0.5f;  ///< ...and play at reduced level.
constexpr int    kMaxClicksPerFrame = 4;
constexpr float  kOccludedGain      = 0.35f;
constexpr float  kOccludedLowpass   = 900.0f;
constexpr float  kUnlatchToCreak    = 0.18f; ///< Creak starts once the latch has released.
} // namespace

bool Soundscape::init() {
    m_bank.build();
    m_enabled = m_audio.init();
    if (!m_enabled) return false;

    VoiceParams loop;
    loop.loop = true;
    loop.reverbSend = 0.0f;
    loop.gain = kHumFloor;
    m_hum = m_audio.play(m_bank.get(SoundId::HumLoop, 0), loop);
    loop.gain = 0.0f;
    m_buzz = m_audio.play(m_bank.get(SoundId::BuzzLoop, 0), loop);
    loop.gain = kDroneGain;
    loop.reverbSend = 0.15f;
    m_drone = m_audio.play(m_bank.get(SoundId::DroneLoop, 0), loop);
    m_lastVariant.fill(-1);
    return true;
}

int Soundscape::pickVariant(SoundId id) {
    const int count = m_bank.variantCount(id);
    int v = m_rng.rangeInt(0, count - 1);
    int& last = m_lastVariant[static_cast<size_t>(id)];
    if (count > 1 && v == last) v = (v + 1) % count; // never the same take twice in a row
    last = v;
    return v;
}

Soundscape::Spatial Soundscape::spatialize(const glm::vec3& source, float refDistance,
                                           const WorldGenerator& generator) const {
    const glm::vec3 d = source - m_listener;
    const float dist = glm::length(d);
    Spatial s{refDistance / std::max(dist, refDistance), 0.0f, 18000.0f, false}; // inverse-distance law

    const glm::vec2 flat(d.x, d.z);
    const float flatLen = glm::length(flat);
    if (flatLen > 1e-3f) {
        const glm::vec2 dir = flat / flatLen;
        const float side = glm::dot(dir, glm::vec2(m_listenerRight.x, m_listenerRight.z));
        const float front = glm::dot(dir, glm::vec2(m_listenerForward.x, m_listenerForward.z));
        s.pan = side * 0.85f * std::min(flatLen, 1.0f);           // sounds at your feet stay centred
        if (front < 0.0f) s.lowpassHz = 18000.0f + (7000.0f - 18000.0f) * -front; // head shadow
    }
    if (generator.isLightBlocked(glm::vec2(m_listener.x, m_listener.z), glm::vec2(source.x, source.z))) {
        s.gain *= kOccludedGain;
        s.lowpassHz = std::min(s.lowpassHz, kOccludedLowpass);
        s.occluded = true;
    }
    return s;
}

void Soundscape::playAt(SoundId id, const glm::vec3& source, float gain, float pitchJitter, float reverbSend,
                        const WorldGenerator& generator, float delay) {
    const Spatial s = spatialize(source, 1.5f, generator);
    VoiceParams p;
    p.gain = gain * s.gain;
    p.pan = s.pan;
    p.lowpassHz = s.lowpassHz;
    p.pitch = 1.0f + m_rng.range(-pitchJitter, pitchJitter);
    p.reverbSend = reverbSend;
    p.delay = delay;
    m_audio.play(m_bank.get(id, pickVariant(id)), p);
}

void Soundscape::play2D(SoundId id, float gain, float pan, float pitch, float reverbSend, float lowpassHz) {
    VoiceParams p;
    p.gain = gain;
    p.pan = pan;
    p.pitch = pitch;
    p.reverbSend = reverbSend;
    p.lowpassHz = lowpassHz;
    m_audio.play(m_bank.get(id, pickVariant(id)), p);
}

void Soundscape::update(float dt, double time, const Player& player, const ChunkManager& chunks,
                        const WorldGenerator& generator) {
    if (!m_enabled) return;
    ++m_frame;
    m_listener = player.eyePosition();
    m_listenerRight = player.camera().right();
    m_listenerForward = player.camera().flatForward();

    handlePlayer(player);
    handleDoors(chunks.doorEvents(), generator);
    updateLights(time, chunks, generator);
    updateDistantEvents(dt);
}

void Soundscape::handlePlayer(const Player& player) {
    for (const PlayerEvent& e : player.events()) {
        switch (e.type) {
        case PlayerEvent::Type::Footstep: {
            const SoundId id = e.elevated ? SoundId::FootHard : SoundId::FootCarpet;
            // Slight left/right offset per foot; tiny pitch spread keeps it organic.
            play2D(id, kFootBase + kFootPerSpeed * e.intensity, 0.05f * static_cast<float>(e.foot),
                   m_rng.range(0.95f, 1.05f), 0.06f);
            break;
        }
        case PlayerEvent::Type::Jump:
            play2D(SoundId::Grunt, kGruntGain, 0.0f, m_rng.range(0.96f, 1.04f), 0.08f);
            // Push-off scuff from the feet.
            play2D(e.elevated ? SoundId::FootHard : SoundId::FootCarpet, 0.25f, 0.0f, m_rng.range(0.95f, 1.05f), 0.05f);
            break;
        case PlayerEvent::Type::Land:
            play2D(e.elevated ? SoundId::LandHard : SoundId::LandCarpet, kLandBase + kLandPerImpact * e.intensity,
                   0.0f, m_rng.range(0.95f, 1.03f), 0.1f);
            break;
        }
    }
}

void Soundscape::handleDoors(const std::vector<DoorEvent>& events, const WorldGenerator& generator) {
    for (const DoorEvent& e : events) {
        const bool unlatched = (e.flags & Door::kEventUnlatch) != 0;
        if (unlatched) playAt(SoundId::DoorUnlatch, e.position, kUnlatchGain, 0.04f, 0.12f, generator);
        if (e.flags & Door::kEventSwing) {
            playAt(SoundId::DoorCreak, e.position, kCreakGain, 0.1f, 0.15f, generator,
                   unlatched ? kUnlatchToCreak : 0.0f);
        }
        if (e.flags & Door::kEventShut) playAt(SoundId::DoorShut, e.position, kShutGain, 0.04f, 0.2f, generator);
    }
}

void Soundscape::updateLights(double time, const ChunkManager& chunks, const WorldGenerator& generator) {
    const ChunkCoord center = ChunkCoord::fromWorld(m_listener.x, m_listener.z);
    float humSum = 0.0f, buzzSum = 0.0f, buzzPan = 0.0f;
    int clicks = 0;

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = chunks.chunkAt({center.x + dx, center.z + dz});
            if (!chunk) continue;
            const std::vector<LightFixture>& lights = chunk->lights();
            for (size_t i = 0; i < lights.size(); ++i) {
                const LightFixture& light = lights[i];
                const float dist = glm::length(light.center() - m_listener);
                if (dist > kLightHearingRange) continue;

                // Same function and clock the renderer uses -> exact visual sync.
                const float intensity = light.intensity(time);
                const Spatial s = spatialize(light.center(), 1.5f, generator);
                const float occlusion = s.occluded ? kOccludedGain : 1.0f;

                // Every ballast adds to the hum while its tube is lit.
                humSum += intensity * occlusion / (1.0f + (dist / 4.0f) * (dist / 4.0f));
                // Faulty ballasts buzz, but only while their tube is actually on.
                if (light.mode() == FlickerMode::Intermittent || light.mode() == FlickerMode::Failing) {
                    const float w = intensity * occlusion / (1.0f + (dist / 2.5f) * (dist / 2.5f));
                    buzzSum += w;
                    buzzPan += w * s.pan;
                }

                // Click exactly when a tube strikes or drops out.
                const uint64_t key = rnd::hashCombine(chunk->seed(), static_cast<uint64_t>(i));
                auto it = m_lights.find(key);
                if (it == m_lights.end()) {
                    m_lights.emplace(key, LightState{intensity, -1.0, m_frame});
                    continue;
                }
                LightState& st = it->second;
                const bool struck = st.previous < kFlickerThreshold && intensity >= kFlickerThreshold;
                const bool dropped = st.previous >= kFlickerThreshold && intensity < kFlickerThreshold;
                if ((struck || dropped) && time - st.lastClick > kMinClickInterval && clicks < kMaxClicksPerFrame) {
                    // A tube stuttering rapidly clicks softer than one striking after a pause.
                    const float stutter = (time - st.lastClick < kStutterWindow) ? kStutterGain : 1.0f;
                    VoiceParams p;
                    p.gain = (struck ? kStrikeGain : kDropGain) * s.gain * stutter;
                    p.pan = s.pan;
                    p.lowpassHz = s.lowpassHz;
                    p.pitch = 1.0f + m_rng.range(-0.06f, 0.06f);
                    p.reverbSend = 0.12f;
                    const SoundId id = struck ? SoundId::LightStrike : SoundId::LightOff;
                    m_audio.play(m_bank.get(id, pickVariant(id)), p);
                    st.lastClick = time;
                    ++clicks;
                }
                st.previous = intensity;
                st.lastSeen = m_frame;
            }
        }
    }

    // Forget lights that have been out of earshot for a while.
    if (m_frame % 120 == 0) {
        for (auto it = m_lights.begin(); it != m_lights.end();) {
            it = (m_frame - it->second.lastSeen > 240) ? m_lights.erase(it) : std::next(it);
        }
    }

    const float hum = kHumFloor + std::min(humSum * kHumPerLight, kHumMax);
    const float buzz = std::min(buzzSum * kBuzzPerLight, kBuzzMax);
    m_audio.setVoice(m_hum, hum, 0.0f, 20000.0f);
    m_audio.setVoice(m_buzz, buzz, buzzSum > 1e-4f ? buzzPan / buzzSum : 0.0f, 20000.0f);
}

void Soundscape::updateDistantEvents(float dt) {
    m_nextDistantEvent -= dt;
    if (m_nextDistantEvent > 0.0f) return;
    m_nextDistantEvent = m_rng.range(6.0f, 20.0f);

    // Somewhere else in the Backrooms, something happens...
    const float roll = m_rng.nextFloat();
    const SoundId id = roll < 0.30f ? SoundId::DistantBang
                     : roll < 0.50f ? SoundId::DistantPounding
                     : roll < 0.75f ? SoundId::DistantFootsteps
                                    : SoundId::DistantMachinery;
    play2D(id, m_rng.range(0.12f, 0.30f), m_rng.range(-0.9f, 0.9f), m_rng.range(0.85f, 1.1f), 0.3f, 2500.0f);
}
