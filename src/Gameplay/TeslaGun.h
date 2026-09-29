#pragma once
// ---------------------------------------------------------------------------
// TeslaGun.h
// The assembled gun's behaviour: holding the trigger discharges the battery
// through the coil, and streamers break out of the spike on the top load.
//
// Discharge. Many times a second a new set of streamers is struck (see
// Gameplay/Lightning): a few that fan out from the spike into the air - or
// into whatever wall, desk or door is in the way, where they end in a shower
// of sparks - and, when something shockable stands within reach inside the
// aim cone, one or two heavier arcs that jump straight onto its body, the
// way a coil's output seeks out a grounded conductor. Every segment of
// every bolt is then collision-tested against the bodies; a body a bolt
// passes through is connected, and burns while it stays connected.
//
// Power. Everything scales with the battery's state of charge: how far the
// streamers reach, how many there are, how thick and bright they are, how
// much light they throw and how fast a connected body burns. A nearly flat
// battery sputters. Firing drains the charge; below kLowBattery the gun
// beeps (faster when nearly flat); a flat gun only clicks.
//
// The gun raises events (sounds, noise) like the terminal console and the
// phone line do, and the engine forwards them.
// ---------------------------------------------------------------------------

#include "Gameplay/Lightning.h"
#include "Math/Random.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class ICollisionWorld;
class IShockable;
class Inventory;
class Physics;

/// A sound the gun made this frame.
struct GunSoundEvent {
    enum class Type : uint8_t {
        Zap,        ///< The crack of the discharge starting.
        Hit,        ///< An arc crackling into a body.
        DryClick,   ///< The trigger pulled on a flat battery.
        LowBattery, ///< The low-charge warning beep.
        Depleted,   ///< The battery just ran flat mid-discharge.
    };
    Type      type;
    glm::vec3 position;
    float     gain;
};

/// The flickering light the arcs throw on their surroundings.
struct ArcLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};
    float     intensity = 0.0f; ///< 0 = off.
    float     range = 1.0f;
};

/// What the gun sees of the world this frame.
struct GunContext {
    glm::vec3 muzzle{0.0f};       ///< World position of the spike's tip.
    glm::vec3 eye{0.0f};          ///< Where the player aims from...
    glm::vec3 aim{0.0f, 0.0f, -1.0f}; ///< ...and along (unit): the crosshair.
    bool      trigger = false;    ///< Held this frame.
    bool      triggerPressed = false; ///< Pulled this frame.
    bool      ready = false;      ///< Assembled and in the player's hands (not mid-swap / assembly).
    const ICollisionWorld* world = nullptr;
    const Physics*         physics = nullptr;
    IShockable*            targets = nullptr;
};

class TeslaGun {
public:
    explicit TeslaGun(uint64_t seed);

    /// Fires (or not), drains the battery in `inventory` and advances bolts and sparks.
    void update(float dt, const GunContext& ctx, Inventory& inventory);

    /// Arcs are streaming out right now.
    bool discharging() const { return m_discharging; }
    /// Strength of the current discharge, 0..1 (follows the charge).
    float power() const { return m_power; }
    /// How hard the gun shakes in the hands, 0..1.
    float vibration() const { return m_discharging ? 0.35f + 0.65f * m_power : 0.0f; }
    /// A body is connected to the arc.
    bool connected() const { return m_connected >= 0; }

    /// Everything to draw: live bolts and the streaks of flying sparks.
    const std::vector<Bolt>& bolts() const { return m_draw; }
    /// Glows out in the world (where arcs land)...
    const std::vector<Glow>& glows() const { return m_glows; }
    /// ...and the corona round the spike (drawn over the gun; intensity 0 = none).
    const Glow& muzzleGlow() const { return m_muzzleGlow; }
    const ArcLight& light() const { return m_light; }

    /// Sounds since the last call.
    std::vector<GunSoundEvent> takeSounds();
    /// Audible radius of the noise the gun made this frame (0: none).
    float noise() const { return m_noise; }

private:
    struct Spark {
        glm::vec3 pos, vel;
        float     age, life;
    };

    void strikeSet(const GunContext& ctx);
    void spray(const glm::vec3& at, const glm::vec3& normal, int count);

    rnd::Rng           m_rng;
    std::vector<Bolt>  m_bolts;  ///< Live discharge channels.
    std::vector<Bolt>  m_draw;   ///< m_bolts plus spark streaks, rebuilt every frame.
    std::vector<Glow>  m_glows;
    Glow               m_muzzleGlow{glm::vec3(0.0f), 0.0f, 0.0f, glm::vec3(0.0f)};
    std::vector<Spark> m_sparks;
    std::vector<GunSoundEvent> m_sounds;
    ArcLight m_light;

    bool      m_discharging = false;
    float     m_power = 0.0f;
    float     m_strikeTimer = 0.0f;
    float     m_flicker = 1.0f;     ///< Brightness of the current strike set.
    int       m_connected = -1;     ///< Target id the last strike set hit, or -1.
    glm::vec3 m_hitPoint{0.0f};
    glm::vec3 m_arcEnd{0.0f};       ///< Mean end point of the last set (for the light).
    float     m_hitSoundTimer = 0.0f;
    float     m_warnTimer = 0.0f;
    bool      m_lowWarned = false;
    float     m_noiseTimer = 0.0f;
    float     m_noise = 0.0f;
};
