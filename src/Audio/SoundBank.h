#pragma once
// ---------------------------------------------------------------------------
// SoundBank.h
// Every sound effect in the game, synthesised procedurally at start-up (no
// audio files). Each sound id owns several randomised variations so repeated
// events (footsteps, door latches...) never sound machine-identical.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
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

private:
    std::array<std::vector<Sound>, kSoundIdCount> m_sounds;
};
