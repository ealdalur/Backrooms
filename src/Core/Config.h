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
inline constexpr int         kWindowWidth  = 640;
inline constexpr int         kWindowHeight = 480;
inline constexpr const char* kWindowTitle  = "Backrooms";

// ----- World ----------------------------------------------------------------
inline constexpr uint64_t kDefaultWorldSeed = 0x0B4C'4000'1CEBull; // "Level 0"
inline constexpr int      kChunkLoadRadius  = 3;  // Chebyshev radius, in chunks
inline constexpr int      kChunkBuildBudget = 2;  // Max new chunks built per frame

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

} // namespace cfg

// ---------------------------------------------------------------------------
// Runtime settings the user can tweak while playing (see Engine key bindings).
// ---------------------------------------------------------------------------
struct Settings {
    float mouseSensitivity = 1.0f;  // Multiplier applied to BOTH look and RMB-drive
    bool  invertY          = false;
};
