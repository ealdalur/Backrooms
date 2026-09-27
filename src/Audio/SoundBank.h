#pragma once
// ---------------------------------------------------------------------------
// SoundBank.h
// Every sound effect in the game, synthesised procedurally at start-up (no
// audio files). Each sound id owns several randomised variations so repeated
// events (footsteps, door latches...) never sound machine-identical.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class SoundId : uint8_t {
    // Seamless loops.
    HumLoop,          ///< Fluorescent ballast hum (120 Hz + harmonics, faint hiss).
    FlickerBuzz,      ///< Malfunctioning-tube "bzzzt": gated per light by its flicker.
    DroneLoop,        ///< Distant, muffled machinery rumble / air handling.
    // Player.
    FootCarpet,       ///< Soft carpet footstep (heel + ball of the foot + fibre scuff).
    FootHard,         ///< Footstep on a desk / cabinet top (hollow knock).
    LandCarpet,       ///< Heavy two-footed landing on carpet.
    LandHard,         ///< Landing on furniture.
    Grunt,            ///< Exerted, low male "huh!" on jump take-off.
    // Doors.
    DoorHandle,       ///< Lever handle "chunk-chunk" as the latch releases.
    DoorCreak,        ///< Hinge creak while the door swings (stick-slip friction).
    DoorShut,         ///< Pitchless "thunk" (enveloped low-passed noise) as the door closes.
    // Distant, muffled ambience (reverb and distance baked in).
    DistantBang,
    DistantPounding,
    DistantFootsteps,
    DistantMachinery,
    // The Wanderer.
    WandererMutter,   ///< Broken, half-whispered phrases ("help me", "where are you"...) - formant speech.
    WandererCry,      ///< The same voice agitated: louder, stuttering, doubled an octave down.
    WandererStep,     ///< Heavy bare footfall with a dragging scuff.
    // The Stalker.
    StalkerSkitter,   ///< Claws scrabbling on carpet in galloping bursts.
    StalkerHiss,      ///< Sharp, whispered exhale as it darts away.
    StalkerBreath,    ///< Seamless loop: slow, wet, rasping breathing.
    // Dread.
    Heartbeat,        ///< Seamless loop: "lub-dub" at 60 bpm (tempo follows playback rate).
    CatchSting,       ///< Dissonant screech and boom when an entity reaches the player.
    // Terminals.
    TerminalKey,      ///< Mechanical keyswitch press + release.
    TerminalBeep,     ///< PC-speaker square-wave beeps.
    TerminalGlitch,   ///< Bit-crushed digital interference (the anomaly breaking through).
    TerminalBoot,     ///< POST beep, hard disk spin-up and seek chatter.
    TerminalOff,      ///< CRT power-down zap.
    CrtHum,           ///< Seamless loop: flyback whine and mains hum of a running monitor.
    Count
};

inline constexpr int kSoundIdCount = static_cast<int>(SoundId::Count);

/// Mono 48 kHz sample data.
struct Sound {
    std::vector<float> samples;
    bool loop = false;
};

class SoundBank {
public:
    /// Synthesises every sound (independent sounds generated in parallel).
    void build();

    int variantCount(SoundId id) const { return static_cast<int>(m_sounds[static_cast<size_t>(id)].size()); }
    const Sound& get(SoundId id, int variant) const;

    /// Writes every variant as a 16-bit mono WAV into `directory` (for
    /// auditioning the synthesis) and prints its peak / RMS level.
    void writeWavFiles(const std::string& directory) const;

private:
    std::array<std::vector<Sound>, kSoundIdCount> m_sounds;
};
