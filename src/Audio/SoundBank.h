#pragma once
// ---------------------------------------------------------------------------
// SoundBank.h
// Every sound effect in the game, synthesised procedurally at start-up (no
// audio files). Each sound id owns several randomised variations so repeated
// events (footsteps, clicks...) never sound machine-identical.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <vector>

enum class SoundId : uint8_t {
    // Seamless loops.
    HumLoop,          ///< Fluorescent ballast hum (120 Hz + harmonics, faint hiss).
    BuzzLoop,         ///< Harsh buzz of a faulty ballast (follows flickering lights).
    DroneLoop,        ///< Distant, muffled machinery rumble / air handling.
    // Player.
    FootCarpet,       ///< Soft carpet footstep (heel + ball of the foot + fibre scuff).
    FootHard,         ///< Footstep on a desk / cabinet top (hollow knock).
    LandCarpet,       ///< Heavy two-footed landing on carpet.
    LandHard,         ///< Landing on furniture.
    Grunt,            ///< Short vocal effort on jump take-off.
    // Doors.
    DoorUnlatch,      ///< Lever turn and latch bolt retracting.
    DoorCreak,        ///< Hinge creak while the door swings (stick-slip friction).
    DoorShut,         ///< Panel thud plus the latch snapping into the strike plate.
    // Lights.
    LightStrike,      ///< Tube catching: starter "tink" and a short buzz burst.
    LightOff,         ///< Dull tick as the arc drops out.
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
