#pragma once
// ---------------------------------------------------------------------------
// Player.h
// First-person kinematic controller: mouse look, WASD / RMB mouse-drive
// locomotion, sprinting, eased crouching (camera AND collision height),
// gravity-integrated jumping with coyote time / jump buffering, velocity-
// linked head bob, landing dip and smoothed step-ups.
// ---------------------------------------------------------------------------

#include "Core/Config.h"
#include "Physics/Physics.h"
#include "Render/Camera.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Input;

/// Something the player's body did this frame (consumed by the soundscape).
struct PlayerEvent {
    enum class Type : uint8_t {
        Footstep, ///< A foot struck the ground (bottom of a head-bob dip).
        Jump,     ///< Take-off.
        Land,     ///< Touch-down after being airborne.
    };
    Type  type;
    float intensity; ///< 0..1: footsteps scale with speed, landings with impact speed.
    bool  elevated;  ///< On top of furniture rather than the carpet.
    int   foot;      ///< -1 left, +1 right (footsteps only).
};

class Player {
public:
    Player(const glm::vec3& spawnFeet, float yaw);

    /// Advances the player by `dt` seconds.
    void update(float dt, const Input& input, const Settings& settings, const ICollisionWorld& world,
                const Physics& physics);

    const Camera& camera() const { return m_camera; }
    const glm::vec3& feetPosition() const { return m_feet; }
    glm::vec3 eyePosition() const { return m_camera.position; }
    glm::vec3 lookDirection() const { return m_camera.forward(); }
    BodyShape shape() const { return {cfg::kPlayerHalfWidth, m_height}; }
    AABB bodyBox() const { return Physics::bodyBox(m_feet, shape()); }
    bool grounded() const { return m_grounded; }
    float horizontalSpeed() const { return glm::length(glm::vec2(m_velocity.x, m_velocity.z)); }
    bool mouseDriveActive() const { return m_mouseDriveActive; }

    /// Events raised during the last update().
    const std::vector<PlayerEvent>& events() const { return m_events; }

private:
    void updateLook(float dt, const Input& input, const Settings& settings);
    void updateCrouch(float dt, const Input& input, const ICollisionWorld& world, const Physics& physics);
    void updateMovement(float dt, const Input& input, const Settings& settings, const ICollisionWorld& world,
                        const Physics& physics);
    void updateCamera(float dt, const Physics& physics);
    float crouchFactor() const;

    Camera    m_camera;
    glm::vec3 m_feet;
    glm::vec3 m_velocity{0.0f};
    float     m_yaw;
    float     m_pitch = 0.0f;

    float m_height = cfg::kStandHeight; ///< Current (eased) collision height.
    bool  m_grounded = false;
    float m_coyoteTimer = 0.0f;
    float m_jumpBuffer = 0.0f;

    bool      m_mouseDriveActive = false;
    glm::vec2 m_mouseDrive{0.0f}; ///< Smoothed RMB drive velocity (x = strafe, y = forward), m/s.
    glm::vec2 m_keyLook{0.0f};    ///< Eased arrow-key look rate (x = pan left, y = tilt up); 1 = full walk-rate hold.

    float m_bobPhase = 0.0f;
    float m_bobWeight = 0.0f;
    float m_landOffset = 0.0f;    ///< Damped-spring camera dip after landing.
    float m_landVelocity = 0.0f;
    float m_stepOffset = 0.0f;    ///< Camera lag that smooths step-ups and mantles.
    float m_stepEaseRate = cfg::kStepEaseRate; ///< Catch-up rate for the current step / mantle (1/s).
    float m_runBlend = 0.0f;      ///< 0..1, drives the sprint FOV kick.

    std::vector<PlayerEvent> m_events;
};
