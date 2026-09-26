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

namespace {
// ---- Mix levels (linear gain; every source is peak-normalised) ----------------
constexpr float kHumFloor        = 0.01f;   ///< Hum that never fully disappears.
constexpr float kHumPerLight     = 0.0175f;
constexpr float kHumMax          = 0.06f;
constexpr float kFlickerBuzzGain = 0.22f;   ///< Malfunction buzz while the faulty tube is lit.
constexpr float kFlickerBuzzIdle = 0.08f;   ///< Fraction still heard between flicker bursts.
constexpr float kDroneGain       = 0.09f;
constexpr float kFootBase        = 0.18f;
constexpr float kFootPerSpeed    = 0.30f;
constexpr float kLandBase        = 0.35f;
constexpr float kLandPerImpact   = 0.50f;
constexpr float kGruntGain       = 0.30f;
constexpr float kHandleGain      = 0.21f;   ///< Lever "chunk-chunk", clearly audible over the creak.
constexpr float kCreakGain       = 0.20f;   ///< Creaks are meant to be subtle.
constexpr float kShutGain        = 0.425f;  ///< Closing "thunk" (energy is mostly below ~400 Hz).

// ---- Behaviour -------------------------------------------------------------------
constexpr float  kLightHearingRange = 14.0f; ///< Lights farther away are inaudible.
constexpr float  kBuzzRange         = 11.0f; ///< Faulty tubes farther away get no buzz voice.
constexpr size_t kMaxBuzzVoices     = 6;     ///< Nearest faulty tubes that buzz at once.
constexpr float  kBuzzGateLow       = 0.35f; ///< Tube output at or below this is "off": no buzz...
constexpr float  kBuzzGateHigh      = 0.90f; ///< ...and at or above this, full buzz.
constexpr float  kOccludedGain      = 0.35f;
constexpr float  kOccludedLowpass   = 900.0f;
/// Occlusion is tested up to a point this far in front of the source (towards
/// the listener). It exceeds half a wall's thickness, so sources mounted in a
/// wall or doorway never count their own wall as an obstruction.
constexpr float  kSourceClearance   = 0.15f;
constexpr float  kHandleToCreak     = 0.20f; ///< Creak starts after the handle's "chunk-chunk".

inline float smoothGate(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
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

glm::vec2 Soundscape::occlusionProbe(const glm::vec3& source) const {
    const glm::vec2 src(source.x, source.z);
    const glm::vec2 toListener = glm::vec2(m_listener.x, m_listener.z) - src;
    const float len = glm::length(toListener);
    if (len <= kSourceClearance) return glm::vec2(m_listener.x, m_listener.z); // right at the source
    return src + toListener * (kSourceClearance / len);
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
    if (generator.isLightBlocked(glm::vec2(m_listener.x, m_listener.z), occlusionProbe(source))) {
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
            play2D(SoundId::Grunt, kGruntGain, 0.0f, m_rng.range(0.97f, 1.03f), 0.08f);
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
        // Opening from closed: the handle goes "chunk-chunk", then the hinges creak.
        const bool unlatched = (e.flags & Door::kEventUnlatch) != 0;
        if (unlatched) playAt(SoundId::DoorHandle, e.position, kHandleGain, 0.03f, 0.1f, generator);
        if (e.flags & Door::kEventSwing) {
            playAt(SoundId::DoorCreak, e.position, kCreakGain, 0.1f, 0.15f, generator,
                   unlatched ? kHandleToCreak : 0.0f);
        }
        // Closing: "thunk" the moment the panel meets the frame. A small reverb
        // send keeps the room's comb filters from colouring it with a pitch.
        if (e.flags & Door::kEventShut) playAt(SoundId::DoorShut, e.position, kShutGain, 0.04f, 0.08f, generator);
    }
}

void Soundscape::updateLights(double time, const ChunkManager& chunks, const WorldGenerator& generator) {
    const ChunkCoord center = ChunkCoord::fromWorld(m_listener.x, m_listener.z);
    float humSum = 0.0f;
    m_buzzCandidates.clear();

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

                const uint64_t key = rnd::hashCombine(chunk->seed(), static_cast<uint64_t>(i));
                LightState& st = m_lights[key];
                st.lastSeen = m_frame;

                // Faulty tubes buzz while their arc is lit: silent in the dim
                // "off" moments of a flicker, full on each flash, and only a
                // faint residue between bursts.
                if ((light.mode() == FlickerMode::Intermittent || light.mode() == FlickerMode::Failing) &&
                    dist <= kBuzzRange) {
                    const float lit = smoothGate(kBuzzGateLow, kBuzzGateHigh, intensity);
                    const float activity = light.isMalfunctioning(time) ? 1.0f : kFlickerBuzzIdle;
                    m_buzzCandidates.push_back(
                        {&st, key, dist, kFlickerBuzzGain * lit * activity * s.gain, s.pan, s.lowpassHz});
                }
            }
        }
    }

    updateBuzzVoices();

    // Forget lights that have been out of earshot for a while.
    if (m_frame % 120 == 0) {
        for (auto it = m_lights.begin(); it != m_lights.end();) {
            if (m_frame - it->second.lastSeen > 240) {
                if (it->second.buzz) m_audio.stop(it->second.buzz);
                it = m_lights.erase(it);
            } else {
                ++it;
            }
        }
    }

    const float hum = kHumFloor + std::min(humSum * kHumPerLight, kHumMax);
    m_audio.setVoice(m_hum, hum, 0.0f, 20000.0f);
}

void Soundscape::updateBuzzVoices() {
    // The nearest faulty tubes get (or keep) a positional buzz voice.
    std::sort(m_buzzCandidates.begin(), m_buzzCandidates.end(),
              [](const BuzzCandidate& a, const BuzzCandidate& b) { return a.distance < b.distance; });
    const size_t count = std::min(m_buzzCandidates.size(), kMaxBuzzVoices);
    for (size_t i = 0; i < count; ++i) {
        const BuzzCandidate& c = m_buzzCandidates[i];
        LightState& st = *c.state;
        if (st.buzz == 0 || !m_audio.isPlaying(st.buzz)) {
            // Variant, pitch and loop phase come from the light's key, so each
            // faulty tube keeps its own recognisable buzz when you return to it.
            VoiceParams p;
            p.loop = true;
            p.gain = 0.0f; // start silent: the ramp below avoids a click mid-waveform
            p.pan = c.pan;
            p.lowpassHz = c.lowpassHz;
            p.reverbSend = 0.1f;
            p.pitch = 0.97f + 0.06f * rnd::toUnit(rnd::hashCombine(c.key, 1));
            p.startOffset = rnd::toUnit(rnd::hashCombine(c.key, 2));
            const int variant = static_cast<int>(c.key % static_cast<uint64_t>(m_bank.variantCount(SoundId::FlickerBuzz)));
            st.buzz = m_audio.play(m_bank.get(SoundId::FlickerBuzz, variant), p);
        }
        // Gain follows the flicker every frame; the mixer's few-ms smoothing
        // keeps each "bzzt" edge sharp but click-free.
        m_audio.setVoice(st.buzz, c.gain, c.pan, c.lowpassHz);
        st.buzzFrame = m_frame;
    }

    // Lights that dropped out of range, or were outranked by nearer ones, fall silent.
    for (auto& kv : m_lights) {
        LightState& st = kv.second;
        if (st.buzz && st.buzzFrame != m_frame) {
            m_audio.stop(st.buzz, 0.15f);
            st.buzz = 0;
        }
    }
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
    play2D(id, 2.0f * m_rng.range(0.12f, 0.30f), m_rng.range(-0.9f, 0.9f), m_rng.range(0.85f, 1.1f), 0.3f, 2500.0f);
}
