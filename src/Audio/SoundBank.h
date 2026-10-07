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
    // The terminal Doom clone (Audio/DoomSounds): 11 kHz / 8-bit effects and FM music.
    DoomPistol,       ///< Pistol shot.
    DoomShotgun,      ///< Shotgun blast and pump.
    DoomImpSight,     ///< Imp spotting the player: raspy screech.
    DoomTrooperSight, ///< Trooper spotting the player: gargled growl.
    DoomMonsterPain,  ///< A monster hit.
    DoomMonsterDeath, ///< Falling scream, gurgle and thud.
    DoomClaw,         ///< Imp melee swipe.
    DoomFireball,     ///< Fireball launch.
    DoomExplode,      ///< Fireball impact / barrel explosion.
    DoomPlayerPain,   ///< "Oof."
    DoomPlayerDeath,  ///< The marine dies.
    DoomItemUp,       ///< Item pickup chirp.
    DoomWeaponUp,     ///< Weapon pickup: double clack.
    DoomDoor,         ///< Door grinding open or shut.
    DoomSwitch,       ///< Wall switch.
    DoomMusic,        ///< Seamless loop: the level music.
    // Office phones (Audio/PhoneSounds): all but the desk-side handling are heard in the earpiece.
    PhoneLine,        ///< Seamless loop: an open line - hiss, mains buzz, crackle.
    PhoneDialTone,    ///< Seamless loop: dial tone (350 + 440 Hz).
    PhoneRingback,    ///< Seamless loop: ringing at the far end (440 + 480 Hz, 2 s on, 4 s off).
    PhoneBusy,        ///< Seamless loop: busy signal (480 + 620 Hz, 0.5 s cadence).
    PhoneReorder,     ///< Seamless loop: fast busy (480 + 620 Hz, 0.25 s cadence).
    PhoneHowler,      ///< Seamless loop: the off-hook warning howl.
    PhoneDtmf,        ///< Touch-tone pairs; variant = key, in keypad order (1-9, *, 0, #).
    PhoneKey,         ///< A keypad button pressed (at the desk).
    PhonePickup,      ///< The handset lifted off its cradle (at the desk).
    PhoneHangup,      ///< The handset put back in its cradle: clunk and hook-switch click (at the desk).
    PhoneSwitching,   ///< Exchange relays clicking as a call is routed.
    PhoneSit,         ///< Special information tones ahead of a recording.
    PhoneOperator,    ///< Intercept announcements; variant = phonesfx::Announcement.
    PhoneStatic,      ///< A burst of interference: something forcing its way onto the line.
    PhoneVoice,       ///< Voices on the line; variant = phonesfx::Voice.
    PhoneBreath,      ///< Someone breathing into a mouthpiece.
    PhoneJenny,       ///< 867-5309: Jenny picks up, and she is sick of it.
    PhoneBeep,        ///< Voicemail signals; variant = phonesfx::Beep.
    PhoneMessage,     ///< Messages left in voicemail; variant = phonesfx::Message.
    // The Tesla coil gun, filing cabinets and the entities under the arc (Audio/TeslaSounds).
    PartPickup,       ///< Grabbing a part: a scuff, a hard clack, loose bits rattling inside.
    PartSwap,         ///< Setting the held part down, then taking the other.
    AssembleSnap,     ///< A part snapping into the gun: a noisy "chhk" (some with ratchet clicks).
    WeaponPowerUp,    ///< Relay clack, the rising whine of the capacitors charging, a test spark.
    TeslaArc,         ///< Seamless loop: the roaring, crackling buzz of the discharge.
    TeslaZap,         ///< A sharp crack: the discharge breaking out, an arc landing.
    TeslaDryClick,    ///< The trigger on a flat battery: a dry click and a relay tick (noise only).
    BatteryLow,       ///< Piezo low-battery chirp: bee-boo.
    CabinetOpen,      ///< A steel drawer rolling out on its runners to the stop.
    CabinetClose,     ///< ...rolling back in and slamming shut.
    WandererPain,     ///< Its electrified shriek.
    WandererDeath,    ///< Its last wail, sagging into a gurgle.
    StalkerPain,      ///< An inhuman, electrified screech.
    StalkerDeath,     ///< The screech coming apart into hissing steam.
    Vaporize,         ///< Frying crackle, a deep whump and steam: a body burning away.
    // The way out (Audio/PhoneSounds, Audio/StorySounds).
    PhoneClue,        ///< The number on the wall answers: "I was able to overwrite the memory at 7A9F..."
    PhoneBell,        ///< A desk phone ringing in the room (one 2 s ring of the cadence).
    NoclipTear,       ///< Reality tearing as the player noclips out of the Backrooms.
    Monologue,        ///< The player, in the office: "Phew, back in the real world..."
    WorkGrumble,      ///< The Wanderer at its office desk: "Oh man, work sucks"; variant = the line (storysfx::workGrumbleText).
    WorkSnarl,        ///< The Stalker at its office desk: a wet, rattling snarl.
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
    /// Length of a variant in seconds.
    float duration(SoundId id, int variant) const;

    /// Writes every variant as a 16-bit mono WAV into `directory` (for
    /// auditioning the synthesis) and prints its peak / RMS level.
    void writeWavFiles(const std::string& directory) const;

private:
    std::array<std::vector<Sound>, kSoundIdCount> m_sounds;
};
