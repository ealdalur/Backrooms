#pragma once
// ---------------------------------------------------------------------------
// Soundscape.h
// Game-side audio director. Translates world state into sound:
//   * Ambient bed: faint ballast hum whose level follows the live intensity
//     of the nearby fluorescent tubes, plus a distant machinery drone.
//   * Malfunction buzz: each nearby faulty tube gets its own positional
//     "bzzzt" voice, gated every frame by that tube's flicker so the buzz
//     lands exactly on its flashes.
//   * Sparse distant, muffled events (bangs, pounding, footsteps, machinery).
//   * Player footsteps / jump grunt / landings from PlayerEvents.
//   * Door unlatch / creak / shut from DoorEvents.
// Positional sounds get distance attenuation, equal-power panning, head
// shadow and occlusion (muffled and quieter when a wall is in the way).
// ---------------------------------------------------------------------------

#include "Audio/AudioEngine.h"
#include "Audio/SoundBank.h"
#include "Math/Random.h"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class ChunkManager;
class Player;
class WorldGenerator;
struct DoorEvent;

class Soundscape {
public:
    /// Synthesises the sound bank, opens the audio device and starts the
    /// ambient loops. Returns false if no audio device exists (the game
    /// then simply runs silently).
    bool init();

    /// Call once per simulated frame with the same `time` the renderer uses
    /// for light flicker, so audio and visuals stay in sync.
    void update(float dt, double time, const Player& player, const ChunkManager& chunks,
                const WorldGenerator& generator);

    void setPaused(bool paused) { m_audio.setPaused(paused); }

private:
    struct Spatial {
        float gain;
        float pan;
        float lowpassHz;
        bool  occluded; ///< A wall stands between the source and the listener.
    };
    struct LightState {
        uint64_t    lastSeen = 0;   ///< Frame index, for pruning.
        VoiceHandle buzz = 0;       ///< Malfunction buzz voice, if this light has one.
        uint64_t    buzzFrame = 0;  ///< Frame the buzz voice was last kept alive.
    };
    /// A faulty light close enough to be given a buzz voice this frame.
    struct BuzzCandidate {
        LightState* state;
        uint64_t    key;
        float       distance;
        float       gain;
        float       pan;
        float       lowpassHz;
    };

    Spatial spatialize(const glm::vec3& source, float refDistance, const WorldGenerator& generator) const;
    /// Point (x/z) just in front of `source` on the listener's side, used as
    /// the far end of the occlusion test.
    glm::vec2 occlusionProbe(const glm::vec3& source) const;
    void playAt(SoundId id, const glm::vec3& source, float gain, float pitchJitter, float reverbSend,
                const WorldGenerator& generator, float delay = 0.0f);
    void play2D(SoundId id, float gain, float pan, float pitch, float reverbSend, float lowpassHz = 20000.0f);
    int pickVariant(SoundId id);

    void handlePlayer(const Player& player);
    void handleDoors(const std::vector<DoorEvent>& events, const WorldGenerator& generator);
    void updateLights(double time, const ChunkManager& chunks, const WorldGenerator& generator);
    void updateBuzzVoices();
    void updateDistantEvents(float dt);

    AudioEngine m_audio;
    SoundBank   m_bank;
    rnd::Rng    m_rng{0xA0D10ull};
    bool        m_enabled = false;

    glm::vec3 m_listener{0.0f};
    glm::vec3 m_listenerRight{1.0f, 0.0f, 0.0f};
    glm::vec3 m_listenerForward{0.0f, 0.0f, -1.0f};

    VoiceHandle m_hum = 0;
    VoiceHandle m_drone = 0;

    float    m_nextDistantEvent = 7.0f;
    uint64_t m_frame = 0;
    std::array<int, kSoundIdCount> m_lastVariant{};
    std::unordered_map<uint64_t, LightState> m_lights;
    std::vector<BuzzCandidate> m_buzzCandidates; ///< Scratch, reused every frame.
};
