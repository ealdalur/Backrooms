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
// The Wanderer's voice is at full level within kVoiceRefDistance and falls off
// with 1/distance beyond it. Loudness from afar depends on gain x reference
// distance; the close-range level on gain alone, so the two are tuned together.
constexpr float kMutterGain      = 0.41f;   ///< The Wanderer murmuring to itself...
constexpr float kCryGain         = 0.64f;   ///< ...and crying out when it has heard you.
constexpr float kVoiceRefDistance = 5.33f;  ///< Its voice carries: audible from far away.
constexpr float kWandererStepGain = 0.45f;
constexpr float kSkitterGain     = 0.5f;
constexpr float kHissGain        = 0.55f;
constexpr float kBreathGain      = 0.32f;
constexpr float kBreathRange     = 6.5f;
constexpr float kHeartGain       = 0.55f;
constexpr float kStingGain       = 0.8f;
constexpr float kMonitorHumGain  = 0.05f;
constexpr float kTerminalMusicGain = 0.8f; // DOOM's music from the terminal's little speaker
constexpr float kHeldPan         = 0.18f;   ///< The gun is held right of centre.
constexpr float kArcGain         = 0.55f;
constexpr float kWandererPainGain  = 0.8f;
constexpr float kWandererDeathGain = 0.85f;
constexpr float kStalkerPainGain   = 0.75f;
constexpr float kStalkerDeathGain  = 0.8f;
constexpr float kVaporizeGain      = 0.7f;
// The handset is held to the right ear, right against it.
constexpr float kEarpieceGain    = 0.55f;
constexpr float kEarpiecePan     = 0.35f;
constexpr float kPhoneToneGain   = 0.5f;
constexpr float kPhoneLineGain   = 0.14f;

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
    // Entity loops idle silently until something is near.
    loop.gain = 0.0f;
    loop.reverbSend = 0.1f;
    m_breath = m_audio.play(m_bank.get(SoundId::StalkerBreath, 0), loop);
    loop.reverbSend = 0.0f;
    m_heart = m_audio.play(m_bank.get(SoundId::Heartbeat, 0), loop);
    m_lastVariant.fill(-1);
    m_voiceGap = 2.0f;
    return true;
}

void Soundscape::updateEntities(float dt, const EntityAudioState& state, const std::vector<EntitySound>& sounds,
                                float fear, const WorldGenerator& generator) {
    if (!m_enabled) return;
    for (const EntitySound& s : sounds) {
        switch (s.type) {
        case EntitySound::Type::StalkerSkitter:
            playAt(SoundId::StalkerSkitter, s.position, kSkitterGain * (0.4f + 0.6f * s.intensity), 0.08f, 0.2f, generator);
            break;
        case EntitySound::Type::StalkerHiss:
            playAt(SoundId::StalkerHiss, s.position, kHissGain, 0.05f, 0.25f, generator);
            break;
        case EntitySound::Type::WandererStep:
            playAt(SoundId::WandererStep, s.position, kWandererStepGain * s.intensity, 0.06f, 0.15f, generator);
            break;
        case EntitySound::Type::WandererPain:
            playAt(SoundId::WandererPain, s.position, kWandererPainGain, 0.05f, 0.3f, generator);
            break;
        case EntitySound::Type::WandererDeath:
            playAt(SoundId::WandererDeath, s.position, kWandererDeathGain, 0.02f, 0.35f, generator);
            break;
        case EntitySound::Type::StalkerPain:
            playAt(SoundId::StalkerPain, s.position, kStalkerPainGain, 0.06f, 0.3f, generator);
            break;
        case EntitySound::Type::StalkerDeath:
            playAt(SoundId::StalkerDeath, s.position, kStalkerDeathGain, 0.02f, 0.35f, generator);
            break;
        case EntitySound::Type::Vaporize:
            playAt(SoundId::Vaporize, s.position, kVaporizeGain, 0.03f, 0.3f, generator);
            break;
        }
    }

    // ---- The Wanderer never stops talking. Each phrase is a positional voice
    //      re-spatialised every frame, so the muttering swells as it closes in.
    if (state.wandererActive) {
        const bool agitated = state.wandererAgitation > 0.55f;
        const Spatial s = spatialize(state.wandererHead, kVoiceRefDistance, generator);
        const float gain = (agitated ? kCryGain : kMutterGain) * s.gain;
        if (m_voice && m_audio.isPlaying(m_voice)) {
            m_audio.setVoice(m_voice, gain, s.pan, s.lowpassHz);
        } else if ((m_voiceGap -= dt) <= 0.0f) {
            const SoundId id = agitated ? SoundId::WandererCry : SoundId::WandererMutter;
            VoiceParams p;
            p.gain = gain;
            p.pan = s.pan;
            p.lowpassHz = s.lowpassHz;
            p.pitch = m_rng.range(0.94f, 1.04f);
            p.reverbSend = 0.35f;
            m_voice = m_audio.play(m_bank.get(id, pickVariant(id)), p);
            m_voiceGap = agitated ? m_rng.range(0.3f, 1.2f) : m_rng.range(1.0f, 3.2f);
        }
    } else if (m_voice) {
        m_audio.stop(m_voice, 0.6f);
        m_voice = 0;
    }

    // ---- Breathing when the Stalker is right there (usually behind you).
    float breath = 0.0f, pan = 0.0f, lowpass = 20000.0f;
    if (state.stalkerActive) {
        const float d = glm::length(state.stalkerPosition - m_listener);
        if (d < kBreathRange) {
            const Spatial s = spatialize(state.stalkerPosition, 1.0f, generator);
            breath = kBreathGain * (1.0f - d / kBreathRange) * (s.occluded ? kOccludedGain : 1.0f);
            pan = s.pan;
            lowpass = s.lowpassHz;
        }
    }
    m_audio.setVoice(m_breath, breath, pan, lowpass);

    // ---- Heartbeat: louder and faster with fear.
    m_audio.setVoice(m_heart, kHeartGain * std::pow(fear, 1.5f), 0.0f, 20000.0f);
    m_audio.setVoicePitch(m_heart, 1.0f + 0.9f * fear);
}

void Soundscape::playEffect(SoundId id, const glm::vec3& at, float gain, const WorldGenerator& generator, float refDistance,
                            int variant) {
    if (!m_enabled) return;
    playAt(id, at, gain, 0.04f, 0.1f, generator, 0.0f, refDistance, variant);
}

void Soundscape::setMonitorHum(bool on, const glm::vec3& at, const WorldGenerator& generator) {
    if (!m_enabled) return;
    if (!on) {
        if (m_monitor) m_audio.stop(m_monitor, 0.2f);
        m_monitor = 0;
        return;
    }
    const Spatial s = spatialize(at, 0.6f, generator);
    if (!m_monitor || !m_audio.isPlaying(m_monitor)) {
        VoiceParams p;
        p.loop = true;
        p.gain = 0.0f;
        p.reverbSend = 0.0f;
        m_monitor = m_audio.play(m_bank.get(SoundId::CrtHum, 0), p);
    }
    m_audio.setVoice(m_monitor, kMonitorHumGain * s.gain, s.pan, s.lowpassHz);
}

void Soundscape::setTerminalMusic(bool on, const glm::vec3& at, const WorldGenerator& generator) {
    if (!m_enabled) return;
    if (!on) {
        if (m_music) m_audio.stop(m_music, 0.3f);
        m_music = 0;
        return;
    }
    const Spatial s = spatialize(at, 0.6f, generator);
    if (!m_music || !m_audio.isPlaying(m_music)) {
        VoiceParams p;
        p.loop = true;
        p.gain = 0.0f;
        p.reverbSend = 0.15f;
        m_music = m_audio.play(m_bank.get(SoundId::DoomMusic, 0), p);
    }
    m_audio.setVoice(m_music, kTerminalMusicGain * s.gain, s.pan, s.lowpassHz);
}

void Soundscape::playSting() {
    if (!m_enabled) return;
    play2D(SoundId::CatchSting, kStingGain, 0.0f, 1.0f, 0.3f);
}

void Soundscape::playInHead(SoundId id, float gain) {
    if (!m_enabled) return;
    play2D(id, gain, 0.0f, 1.0f, 0.05f);
}

void Soundscape::playHeld(SoundId id, float gain, float pitch) {
    if (!m_enabled) return;
    play2D(id, gain, kHeldPan, pitch * (1.0f + m_rng.range(-0.03f, 0.03f)), 0.2f);
}

void Soundscape::setArcLoop(bool on, float gain, float pitch) {
    if (!m_enabled) return;
    if (!on) {
        if (m_arc) m_audio.stop(m_arc, 0.08f);
        m_arc = 0;
        return;
    }
    if (!m_arc || !m_audio.isPlaying(m_arc)) {
        VoiceParams p;
        p.loop = true;
        p.gain = 0.0f; // the mixer's smoothing fades it in
        p.pan = kHeldPan;
        p.reverbSend = 0.3f;                 // it fills the room
        p.startOffset = m_rng.nextFloat();   // never the same crackle twice
        m_arc = m_audio.play(m_bank.get(SoundId::TeslaArc, 0), p);
    }
    m_audio.setVoice(m_arc, kArcGain * gain, kHeldPan, 20000.0f);
    m_audio.setVoicePitch(m_arc, pitch);
}

void Soundscape::setPhoneLine(SoundId tone, float toneGain, float lineGain) {
    if (!m_enabled) return;
    auto loopAt = [this](VoiceHandle& voice, SoundId id, float gain) {
        if (!voice || !m_audio.isPlaying(voice)) {
            VoiceParams p;
            p.loop = true;
            p.gain = 0.0f; // the mixer's smoothing fades it in without a click
            p.pan = kEarpiecePan;
            p.reverbSend = 0.0f;
            voice = m_audio.play(m_bank.get(id, 0), p);
        }
        m_audio.setVoice(voice, gain, kEarpiecePan, 20000.0f);
    };
    loopAt(m_phoneLine, SoundId::PhoneLine, kPhoneLineGain * lineGain);
    if (tone != m_phoneToneId) {
        if (m_phoneTone) m_audio.stop(m_phoneTone, 0.01f);
        m_phoneTone = 0;
        m_phoneToneId = tone;
    }
    if (tone != SoundId::Count) loopAt(m_phoneTone, tone, kPhoneToneGain * toneGain);
}

void Soundscape::playEarpiece(SoundId id, int variant, float gain, float delay) {
    if (!m_enabled) return;
    m_earpiece.erase(std::remove_if(m_earpiece.begin(), m_earpiece.end(), [this](VoiceHandle h) { return !m_audio.isPlaying(h); }),
                     m_earpiece.end());
    VoiceParams p;
    p.gain = kEarpieceGain * gain;
    p.pan = kEarpiecePan;
    p.reverbSend = 0.0f;
    p.delay = delay;
    const VoiceHandle h = m_audio.play(m_bank.get(id, variant < 0 ? pickVariant(id) : variant), p);
    if (h) m_earpiece.push_back(h);
}

void Soundscape::stopEarpieceSounds() {
    if (!m_enabled) return;
    for (VoiceHandle h : m_earpiece) m_audio.stop(h, 0.01f);
    m_earpiece.clear();
}

void Soundscape::stopEarpiece() {
    if (!m_enabled) return;
    stopEarpieceSounds();
    if (m_phoneLine) m_audio.stop(m_phoneLine, 0.01f);
    if (m_phoneTone) m_audio.stop(m_phoneTone, 0.01f);
    m_phoneLine = m_phoneTone = 0;
    m_phoneToneId = SoundId::Count;
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
    // Another storey is heard through the slab: always occluded.
    const bool otherLevel = world::levelOf(source.y) != m_listenerLevel;
    if (otherLevel || generator.isLightBlocked(m_listenerLevel, glm::vec2(m_listener.x, m_listener.z), occlusionProbe(source))) {
        s.gain *= kOccludedGain;
        s.lowpassHz = std::min(s.lowpassHz, kOccludedLowpass);
        s.occluded = true;
    }
    return s;
}

void Soundscape::playAt(SoundId id, const glm::vec3& source, float gain, float pitchJitter, float reverbSend,
                        const WorldGenerator& generator, float delay, float refDistance, int variant) {
    const Spatial s = spatialize(source, refDistance, generator);
    VoiceParams p;
    p.gain = gain * s.gain;
    p.pan = s.pan;
    p.lowpassHz = s.lowpassHz;
    p.pitch = 1.0f + m_rng.range(-pitchJitter, pitchJitter);
    p.reverbSend = reverbSend;
    p.delay = delay;
    m_audio.play(m_bank.get(id, variant >= 0 ? variant : pickVariant(id)), p);
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
    m_listenerLevel = world::levelOf(player.feetPosition().y);
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
    const ChunkCoord center = ChunkCoord::fromWorld(m_listener.x, m_listener.z, m_listenerLevel);
    float humSum = 0.0f;
    m_buzzCandidates.clear();

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const Chunk* chunk = chunks.chunkAt({center.x + dx, center.z + dz, center.level});
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
