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
//   * Door handle "chunk-chunk", hinge creak and shut "thunk" from DoorEvents.
//   * Entities: the Wanderer's endlessly looping, broken phrases (tracked
//     from its head every frame, so they swell as it approaches), its
//     dragging steps, the Stalker's skittering and hiss, and its breathing
//     when it is right behind you; a heartbeat that quickens with fear.
//   * Terminals: key clicks, beeps, glitches, boot and the monitor's hum -
//     and, when DOOM is running on one, its effects and looping music.
//   * Phones: the earpiece of a lifted handset - the open line, one looping
//     call-progress tone and whatever else comes down the wire - played at
//     the listener's ear, not in the world.
//   * The Tesla gun in the player's hands: its roaring discharge loop (level
//     and pitch following the battery), cracks, clicks and warning beeps, and
//     parts being handled; filing-cabinet drawers; the entities' screams of
//     pain and death, and the sizzle of a body vaporising.
// Positional sounds get distance attenuation, equal-power panning, head
// shadow and occlusion (muffled and quieter when a wall is in the way, and
// always when the source is on another storey).
// ---------------------------------------------------------------------------

#include "AI/Perception.h"
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
    Soundscape() = default;
    /// Stops the audio thread before the sound bank it reads from is freed
    /// (members are destroyed in reverse order, bank first).
    ~Soundscape() { m_audio.shutdown(); }
    Soundscape(const Soundscape&) = delete;
    Soundscape& operator=(const Soundscape&) = delete;

    /// Synthesises the sound bank, opens the audio device and starts the
    /// ambient loops. Returns false if no audio device exists (the game
    /// then simply runs silently).
    bool init();

    /// Call once per simulated frame with the same `time` the renderer uses
    /// for light flicker, so audio and visuals stay in sync.
    void update(float dt, double time, const Player& player, const ChunkManager& chunks,
                const WorldGenerator& generator);

    void setPaused(bool paused) { m_audio.setPaused(paused); }

    /// Entity voices, breathing, footsteps and the heartbeat. Call once per
    /// frame after update().
    void updateEntities(float dt, const EntityAudioState& state, const std::vector<EntitySound>& sounds, float fear,
                        const WorldGenerator& generator);

    /// A positional one-shot (terminal keys, beeps...).
    void playEffect(SoundId id, const glm::vec3& at, float gain, const WorldGenerator& generator);

    /// Hum of the monitor in use; `on` = false silences it.
    void setMonitorHum(bool on, const glm::vec3& at, const WorldGenerator& generator);

    /// DOOM's music from the terminal at `at`; `on` = false stops it.
    void setTerminalMusic(bool on, const glm::vec3& at, const WorldGenerator& generator);

    /// The shock when an entity reaches the player.
    void playSting();

    /// A sound in the player's own head (their voice, the noclip): centred, not positional.
    void playInHead(SoundId id, float gain);

    /// A sound in the player's hands (the gun, a part being handled): not
    /// positional, slightly right of centre.
    void playHeld(SoundId id, float gain, float pitch = 1.0f);
    /// The discharge loop: `on` = false fades it out.
    void setArcLoop(bool on, float gain, float pitch);

    /// The earpiece of the handset in the player's hand: the open line's hiss
    /// at `lineGain` and `tone` (a looping call-progress tone, SoundId::Count
    /// for none) at `toneGain`. A new tone starts at the top of its cadence.
    void setPhoneLine(SoundId tone, float toneGain, float lineGain);
    /// A one-shot in the earpiece; `variant` < 0 picks one at random.
    void playEarpiece(SoundId id, int variant, float gain, float delay);
    /// Hung up: everything in the earpiece stops at once.
    void stopEarpiece();
    /// Cuts off the earpiece's one-shots (speech, beeps), leaving the line and its tone.
    void stopEarpieceSounds();

    /// The synthesised sounds (lengths, variant counts).
    const SoundBank& bank() const { return m_bank; }

    /// Writes the whole synthesised sound bank to WAV files.
    void dumpSounds(const std::string& directory) const { m_bank.writeWavFiles(directory); }

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
    int       m_listenerLevel = 0;
    glm::vec3 m_listenerRight{1.0f, 0.0f, 0.0f};
    glm::vec3 m_listenerForward{0.0f, 0.0f, -1.0f};

    VoiceHandle m_hum = 0;
    VoiceHandle m_drone = 0;
    VoiceHandle m_breath = 0;     ///< Stalker breathing loop (silent unless it is close).
    VoiceHandle m_heart = 0;      ///< Heartbeat loop (gain and tempo follow fear).
    VoiceHandle m_voice = 0;      ///< The Wanderer's current phrase.
    VoiceHandle m_monitor = 0;    ///< CRT hum of the terminal in use.
    VoiceHandle m_music = 0;      ///< DOOM's music, while it runs on that terminal.
    VoiceHandle m_arc = 0;        ///< The Tesla gun's discharge loop.
    VoiceHandle m_phoneLine = 0;  ///< Earpiece: the open line.
    VoiceHandle m_phoneTone = 0;  ///< Earpiece: the current call-progress tone...
    SoundId     m_phoneToneId = SoundId::Count; ///< ...and which one it is.
    std::vector<VoiceHandle> m_earpiece; ///< Earpiece one-shots still playing.
    float       m_voiceGap = 0.0f; ///< Pause before its next phrase.

    float    m_nextDistantEvent = 7.0f;
    uint64_t m_frame = 0;
    std::array<int, kSoundIdCount> m_lastVariant{};
    std::unordered_map<uint64_t, LightState> m_lights;
    std::vector<BuzzCandidate> m_buzzCandidates; ///< Scratch, reused every frame.
};
