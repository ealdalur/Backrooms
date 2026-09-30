#pragma once
// ---------------------------------------------------------------------------
// Config.h
// Central, compile-time tuning constants for the whole simulator plus the
// small set of user-adjustable runtime settings. Keeping every "magic number"
// here makes balancing movement, rendering and generation a one-file job.
// ---------------------------------------------------------------------------

#include <cstdint>

namespace cfg {

// ----- Window ---------------------------------------------------------------
inline constexpr int         kWindowWidth  = 1600;
inline constexpr int         kWindowHeight = 900;
inline constexpr const char* kWindowTitle  = "Backrooms";

// ----- World ----------------------------------------------------------------
inline constexpr uint64_t kDefaultWorldSeed = 0x0B4C'4000'1CEBull; // "Level 0"
inline constexpr int      kChunkLoadRadius  = 3;  // Chebyshev radius, in chunks
inline constexpr int      kChunkAdjacentRadius = 2; // Radius loaded on the storeys above and below (entities walk them to follow you)
inline constexpr int      kChunkBuildBudget = 2;  // Max new chunks built per frame
inline constexpr float    kLevelSwitchBand  = 0.6f; // Fraction of a storey climbed before the focus storey changes

// ----- Rendering ------------------------------------------------------------
inline constexpr float kFieldOfViewDeg   = 70.0f;  // Vertical FOV when walking
inline constexpr float kRunFovBoostDeg   = 5.0f;   // Extra FOV at full sprint
inline constexpr float kNearPlane        = 0.05f;
inline constexpr float kFarPlane         = 220.0f;
inline constexpr int   kMsaaSamples      = 4;
inline constexpr int   kTextureSize      = 1024;   // Procedural material resolution
inline constexpr float kExposure         = 1.05f;
inline constexpr float kBloomStrength    = 0.085f;
inline constexpr float kBloomThreshold   = 1.1f;
inline constexpr float kLightPower       = 5.2f;   // Global fluorescent output scale
inline constexpr float kFogDensity       = 0.026f; // Squared-exponential haze

// ----- Player dimensions ------------------------------------------------------
inline constexpr float kPlayerHalfWidth    = 0.30f;
inline constexpr float kStandHeight        = 1.80f;
inline constexpr float kCrouchHeight       = 1.15f;
inline constexpr float kEyeBelowTop        = 0.16f;  // Eye offset from top of AABB
inline constexpr float kCrouchEaseRate     = 10.0f;  // Exponential ease-out rate (1/s)
inline constexpr float kStepHeight         = 0.32f;  // Auto step-up for small ledges
inline constexpr float kMantleHeight       = 0.35f;  // Mid-air reach: climb onto a ledge this far above the feet
inline constexpr float kStepEaseRate       = 12.0f;  // Camera catch-up after a step-up (1/s)
inline constexpr float kMantleEaseRate     = 7.0f;   // Slower camera rise after a mantle: hauling yourself up

// ----- Player kinematics -------------------------------------------------------
inline constexpr float kWalkSpeed          = 3.2f;   // m/s
inline constexpr float kRunSpeed           = 6.0f;
inline constexpr float kCrouchSpeed        = 1.5f;
inline constexpr float kGroundResponse     = 12.0f;  // Velocity convergence rate on ground (1/s)
inline constexpr float kAirResponse        = 1.6f;   // Limited air control
inline constexpr float kGravity            = 16.0f;  // m/s^2 (snappier than 9.81 for game feel)
inline constexpr float kJumpVelocity       = 5.4f;   // Initial vertical impulse -> ~0.91 m apex
inline constexpr float kTerminalVelocity   = 40.0f;
inline constexpr float kCoyoteTime         = 0.10f;  // Grace period to jump after leaving a ledge
inline constexpr float kJumpBufferTime     = 0.12f;  // Pressing jump slightly early still counts

// ----- Camera feel --------------------------------------------------------------
inline constexpr float kLookRadiansPerPixel   = 0.0022f; // Base look speed (x sensitivity)
inline constexpr float kMouseDriveMetersPerPx = 0.012f;  // RMB drive: metres per mouse pixel (x sensitivity)
inline constexpr float kMouseDriveMaxSpeed    = 8.0f;    // Safety clamp for RMB driving
inline constexpr float kMaxPitchDeg           = 89.0f;
inline constexpr float kKeyPanSpeedDeg        = 120.0f;  // Arrow-key pan at full hold (deg/s, x sensitivity; x run/walk with Shift)
inline constexpr float kKeyTiltSpeedDeg       = 90.0f;   // Arrow-key tilt at full hold (deg/s, x sensitivity; x run/walk with Shift)
inline constexpr float kKeyLookResponse       = 20.0f;   // Arrow-key ease-in rate (1/s): taps nudge, holds turn
// Head bob follows a footstep cadence that rises gently with speed, like a
// real gait (~1.8 steps/s walking, ~2.6 steps/s running), rather than being
// locked to distance travelled (which makes fast movement look jittery).
inline constexpr float kBobCadenceBase        = 0.90f;   // Footsteps per second (extrapolated to zero speed)
inline constexpr float kBobCadencePerSpeed    = 0.28f;   // Extra footsteps per second per m/s
inline constexpr float kBobVerticalAmp        = 0.050f;  // One dip per footstep
inline constexpr float kBobLateralAmp         = 0.035f;  // One sway per stride (two footsteps)

// ----- Interaction --------------------------------------------------------------
inline constexpr float kDoorInteractDistance = 2.2f;
inline constexpr float kDoorSwingDuration    = 0.85f;  // Seconds for a full open/close
inline constexpr float kTerminalOpenTime     = 0.45f;  // Seconds to lean in to / away from a screen

// ----- Entities -------------------------------------------------------------------
// The Stalker: moves only while unobserved, hunts through the dark.
inline constexpr float kStalkerFirstSpawn    = 45.0f;  // Seconds of play before it first appears
inline constexpr float kStalkerSpeed         = 6.8f;   // Unseen approach (faster than the player's run)
inline constexpr float kStalkerCreepSpeed    = 1.6f;   // Closing in on the player...
inline constexpr float kStalkerProwlDist     = 9.0f;   // ...within this distance
inline constexpr float kStalkerDartSpeed     = 11.0f;  // Dash to cover once stared at
inline constexpr float kStalkerLungeSpeed    = 8.5f;   // Final rush
inline constexpr float kStalkerLungeRange    = 2.4f;
inline constexpr float kStalkerExposureLimit = 1.8f;   // Seconds of being watched before it darts away
inline constexpr float kStalkerDarkLevel     = 0.18f;  // Below this light level it cannot be made out...
inline constexpr float kStalkerDarkSightDist = 7.0f;   // ...beyond this distance
inline constexpr float kStalkerDrainRadius   = 7.5f;   // Lights within this radius sag and stutter
inline constexpr int   kStalkerStareDowns    = 3;      // Stared down this many times, it bolts far away...
inline constexpr float kStalkerRetreatMin    = 10.0f;  // ...to a dark spot this far from the player...
inline constexpr float kStalkerRetreatMax    = 20.0f;  // ...but no farther, out of their sight...
inline constexpr float kStalkerLieLowMin     = 7.0f;   // ...and lies low there this long...
inline constexpr float kStalkerLieLowMax     = 18.0f;  // ...to this long before hunting again
// The Wanderer: blind, hunts by sound, never stops talking.
inline constexpr float kWandererFirstSpawn   = 20.0f;
inline constexpr float kWandererRoamSpeed    = 1.0f;
inline constexpr float kWandererSearchSpeed  = 1.9f;
inline constexpr float kWandererHuntSpeed    = 3.3f;   // Faster than walking, slower than running
inline constexpr float kWandererSpawnMin     = 18.0f;  // Appears this far from the player...
inline constexpr float kWandererSpawnMax     = 30.0f;  // ...and no farther, preferably ahead of them
inline constexpr float kWandererDrift        = 0.6f;   // Chance each aimless step leans towards the player
inline constexpr float kWandererRelocateDist = 48.0f;  // Left farther behind than this: it resurfaces nearer
// Both.
inline constexpr float kCatchDistance        = 0.85f;
inline constexpr float kStalkerRelocateDist  = 70.0f;  // Farther than this: it resurfaces nearer
inline constexpr float kFollowTimeout        = 30.0f;  // Left on another storey this long (no way through the stairs found): it resurfaces near the player
// Audible radius (m) of player-made noise; walls halve it.
inline constexpr float kNoiseFootstepRun     = 22.0f;  // Scaled by footstep intensity
inline constexpr float kNoiseLanding         = 18.0f;
inline constexpr float kNoiseDoor            = 16.0f;
inline constexpr float kNoiseTyping          = 7.0f;
inline constexpr float kNoiseMachine         = 10.0f;
inline constexpr float kNoiseDoomGunfire     = 12.0f;  // DOOM's guns and explosions from a terminal speaker
inline constexpr float kNoiseDoomMusic       = 6.0f;   // ...and its music, now and then
inline constexpr float kNoiseCabinet         = 9.0f;   // A filing-cabinet drawer rolling open or slamming shut
inline constexpr float kNoiseTesla           = 24.0f;  // The Tesla gun's roaring discharge: everything hears it

// ----- The Tesla coil gun ---------------------------------------------------------
inline constexpr float kCabinetOpenTime  = 0.45f;  // Seconds to lean in to / away from a filing cabinet
inline constexpr float kAssembleTime     = 2.4f;   // Putting the four parts together
inline constexpr float kSwapDipTime      = 0.7f;   // The gun dips out of view while a part is hot-swapped
inline constexpr float kBatteryLife      = 7.0f;   // Seconds of continuous discharge from a full battery
inline constexpr float kLowBattery       = 0.2f;   // Below this charge the gauge blinks red and the gun beeps
inline constexpr float kArcReachMin      = 1.6f;   // Discharge reach (m) on a nearly flat battery...
inline constexpr float kArcReachMax      = 7.5f;   // ...and on a full one
inline constexpr float kArcStrikeRate    = 34.0f;  // New sets of streamers per second
inline constexpr float kArcConeDeg       = 26.0f;  // Arcs jump to a body within this angle of the aim
inline constexpr float kArcLightPower    = 1.4f;   // Brightness of the flicker the arcs throw on the room
inline constexpr float kShockDps         = 0.55f;  // Health per second a connected arc burns at full power
inline constexpr float kShockRegenDelay  = 2.5f;   // An entity out of the arc this long starts to recover...
inline constexpr float kShockRegenRate   = 0.06f;  // ...this much health per second
inline constexpr float kVaporizeTime     = 3.2f;   // Seconds a dead entity takes to dissolve away

} // namespace cfg

// ---------------------------------------------------------------------------
// Runtime settings the user can tweak while playing (see Engine key bindings).
// ---------------------------------------------------------------------------
struct Settings {
    float mouseSensitivity = 1.0f;  // Multiplier applied to BOTH look and RMB-drive
    bool  invertY          = false;
};
