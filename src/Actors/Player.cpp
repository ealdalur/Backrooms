// ---------------------------------------------------------------------------
// Player.cpp
// ---------------------------------------------------------------------------
#include "Actors/Player.h"

#include "Core/Input.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265359f;
constexpr float kTwoPi = 6.28318530718f;
/// Bob phase at which a foot strikes: the bottom of the vertical dip,
/// where sin(2 * phase) = -1, i.e. 3pi/4 (+ k * pi for alternate feet).
constexpr float kFootStrikePhase = 0.75f * kPi;
/// Standing on furniture rather than the carpet.
constexpr float kElevatedHeight = 0.02f;

/// Frame-rate independent exponential approach factor.
inline float approachFactor(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }
} // namespace

Player::Player(const glm::vec3& spawnFeet, float yaw) : m_feet(spawnFeet), m_yaw(yaw) {
    m_camera.yaw = yaw;
    m_camera.fovYDegrees = cfg::kFieldOfViewDeg;
    m_camera.nearPlane = cfg::kNearPlane;
    m_camera.farPlane = cfg::kFarPlane;
    m_camera.position = m_feet + glm::vec3(0.0f, m_height - cfg::kEyeBelowTop, 0.0f);
}

float Player::crouchFactor() const {
    return std::clamp((cfg::kStandHeight - m_height) / (cfg::kStandHeight - cfg::kCrouchHeight), 0.0f, 1.0f);
}

void Player::update(float dt, const Input& input, const Settings& settings, const ICollisionWorld& world,
                    const Physics& physics) {
    m_events.clear();
    if (dt <= 0.0f) return;
    updateLook(dt, input, settings);
    updateCrouch(dt, input, world, physics);
    updateMovement(dt, input, settings, world, physics);
    updateCamera(dt, physics);
}

void Player::updateLook(float dt, const Input& input, const Settings& settings) {
    // Holding the right mouse button locks mouse look: the mouse drives movement instead.
    m_mouseDriveActive = input.mouseDown(SDL_BUTTON_RIGHT);
    if (!m_mouseDriveActive) {
        const glm::vec2 d = input.mouseDelta();
        const float k = cfg::kLookRadiansPerPixel * settings.mouseSensitivity;
        m_yaw -= d.x * k;
        m_pitch -= d.y * k * (settings.invertY ? -1.0f : 1.0f);
    }

    // Arrow keys pan / tilt for playing without a mouse (always available,
    // even during RMB mouse-drive). The rate eases in, so a tap nudges the
    // view for fine aiming and a hold turns smoothly at full speed.
    glm::vec2 keys(0.0f); // x: +1 pans left, y: +1 tilts up
    if (input.keyDown(SDL_SCANCODE_LEFT)) keys.x += 1.0f;
    if (input.keyDown(SDL_SCANCODE_RIGHT)) keys.x -= 1.0f;
    if (input.keyDown(SDL_SCANCODE_UP)) keys.y += 1.0f;
    if (input.keyDown(SDL_SCANCODE_DOWN)) keys.y -= 1.0f;
    // The run key speeds the view up by the same factor running beats walking.
    // Applied to the eased target, so pressing or releasing it mid-turn is smooth.
    if (input.keyDown(SDL_SCANCODE_LSHIFT)) keys *= cfg::kRunSpeed / cfg::kWalkSpeed;
    m_keyLook += (keys - m_keyLook) * approachFactor(cfg::kKeyLookResponse, dt);
    if (keys == glm::vec2(0.0f) && glm::length(m_keyLook) < 1e-3f) m_keyLook = glm::vec2(0.0f); // no drift after release
    m_yaw += m_keyLook.x * glm::radians(cfg::kKeyPanSpeedDeg) * settings.mouseSensitivity * dt;
    m_pitch += m_keyLook.y * glm::radians(cfg::kKeyTiltSpeedDeg) * settings.mouseSensitivity * dt;

    const float maxPitch = glm::radians(cfg::kMaxPitchDeg);
    m_pitch = std::clamp(m_pitch, -maxPitch, maxPitch);
    m_yaw = std::fmod(m_yaw, kTwoPi);
    // Keep the camera orientation current so movement uses this frame's heading.
    m_camera.yaw = m_yaw;
    m_camera.pitch = m_pitch;
}

void Player::updateCrouch(float dt, const Input& input, const ICollisionWorld& world, const Physics& physics) {
    const float target = input.keyDown(SDL_SCANCODE_C) ? cfg::kCrouchHeight : cfg::kStandHeight;
    // Ease-out: exponential approach towards the target height.
    float next = m_height + (target - m_height) * approachFactor(cfg::kCrouchEaseRate, dt);
    if (std::fabs(next - target) < 1e-3f) next = target;

    if (next > m_height) {
        // Standing up: only grow into free space (no rising through desks/headers).
        const AABB grow(glm::vec3(m_feet.x - cfg::kPlayerHalfWidth, m_feet.y + m_height, m_feet.z - cfg::kPlayerHalfWidth),
                        glm::vec3(m_feet.x + cfg::kPlayerHalfWidth, m_feet.y + next, m_feet.z + cfg::kPlayerHalfWidth));
        if (!physics.isFree(grow, world)) return;
    }
    m_height = next;
}

void Player::updateMovement(float dt, const Input& input, const Settings& settings, const ICollisionWorld& world,
                            const Physics& physics) {
    const glm::vec3 forward = m_camera.flatForward();
    const glm::vec3 right = m_camera.right();
    const float crouch = crouchFactor();

    // ---- Target speed ---------------------------------------------------------------
    glm::vec2 wish(0.0f); // x = strafe, y = forward
    if (input.keyDown(SDL_SCANCODE_W)) wish.y += 1.0f;
    if (input.keyDown(SDL_SCANCODE_S)) wish.y -= 1.0f;
    if (input.keyDown(SDL_SCANCODE_D)) wish.x += 1.0f;
    if (input.keyDown(SDL_SCANCODE_A)) wish.x -= 1.0f;
    if (glm::dot(wish, wish) > 1.0f) wish = glm::normalize(wish);

    const bool sprint = input.keyDown(SDL_SCANCODE_LSHIFT);
    const float baseSpeed = sprint ? cfg::kRunSpeed : cfg::kWalkSpeed;
    const float maxSpeed = glm::mix(baseSpeed, cfg::kCrouchSpeed, crouch);
    const float speedScale = maxSpeed / cfg::kWalkSpeed;

    // ---- RMB mouse drive: mouse velocity (px/s) -> strafe/forward velocity (m/s),
    //      scaled directly by the sensitivity multiplier.
    if (m_mouseDriveActive) {
        const glm::vec2 d = input.mouseDelta();
        const float metresPerPixel = cfg::kMouseDriveMetersPerPx * settings.mouseSensitivity;
        const glm::vec2 raw(d.x * metresPerPixel / dt, -d.y * metresPerPixel / dt); // push mouse away = forward
        m_mouseDrive += (raw - m_mouseDrive) * approachFactor(25.0f, dt);           // de-jitter sparse deltas
    } else {
        m_mouseDrive = glm::vec2(0.0f);
    }
    glm::vec2 drive = m_mouseDrive * speedScale;
    const float driveLimit = cfg::kMouseDriveMaxSpeed * speedScale;
    if (glm::length(drive) > driveLimit) drive = glm::normalize(drive) * driveLimit;

    const glm::vec2 local = wish * maxSpeed + drive;
    const glm::vec3 desired = right * local.x + forward * local.y;

    // ---- Horizontal kinematics: exponential convergence (limited in the air) --------
    const float response = m_grounded ? cfg::kGroundResponse : cfg::kAirResponse;
    const float blend = approachFactor(response, dt);
    m_velocity.x += (desired.x - m_velocity.x) * blend;
    m_velocity.z += (desired.z - m_velocity.z) * blend;

    // ---- Jumping (with coyote time and input buffering) ------------------------------
    m_coyoteTimer = m_grounded ? cfg::kCoyoteTime : std::max(0.0f, m_coyoteTimer - dt);
    m_jumpBuffer = input.keyPressed(SDL_SCANCODE_SPACE) ? cfg::kJumpBufferTime : std::max(0.0f, m_jumpBuffer - dt);
    if (m_jumpBuffer > 0.0f && m_coyoteTimer > 0.0f) {
        m_events.push_back({PlayerEvent::Type::Jump, 1.0f, m_feet.y > kElevatedHeight, 0});
        m_velocity.y = cfg::kJumpVelocity; // initial vertical impulse
        m_jumpBuffer = 0.0f;
        m_coyoteTimer = 0.0f;
        m_grounded = false;
    }

    // ---- Vertical kinematics: exact integration under constant gravity ---------------
    //   y(t+dt) = y + v*dt - g*dt^2/2 ,  v(t+dt) = v - g*dt
    const float preVy = m_velocity.y;
    const float dy = m_velocity.y * dt - 0.5f * cfg::kGravity * dt * dt;
    m_velocity.y = std::max(m_velocity.y - cfg::kGravity * dt, -cfg::kTerminalVelocity);

    // ---- Collide ----------------------------------------------------------------------
    const bool wasGrounded = m_grounded;
    const glm::vec3 displacement(m_velocity.x * dt, dy, m_velocity.z * dt);
    const MoveResult r = physics.move(m_feet, shape(), displacement, wasGrounded,
                                      wasGrounded && m_velocity.y <= 0.0f, world);

    if (r.blockedX) m_velocity.x = 0.0f;
    if (r.blockedZ) m_velocity.z = 0.0f;
    if (r.hitCeiling && m_velocity.y > 0.0f) m_velocity.y = 0.0f;
    if (r.grounded) {
        if (!wasGrounded && preVy < -2.0f) {
            // Landing impact: kick the camera spring downwards proportional to speed.
            m_landVelocity -= std::min(-preVy * 0.3f, 3.0f);
        }
        if (!wasGrounded && preVy < -1.5f) {
            m_events.push_back({PlayerEvent::Type::Land, std::clamp(-preVy / 8.0f, 0.0f, 1.0f),
                                m_feet.y > kElevatedHeight, 0});
            // The next footstep follows about half a step after touching down.
            m_bobPhase = kFootStrikePhase + 0.5f * kPi;
        }
        if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    }
    m_grounded = r.grounded;
    m_stepOffset = std::clamp(m_stepOffset - r.steppedUp, -0.4f, 0.4f);

    // Sprint blend for FOV: only when actually running forward on the ground.
    const bool running = sprint && wish.y > 0.1f && crouch < 0.5f && horizontalSpeed() > cfg::kWalkSpeed * 1.1f;
    m_runBlend += ((running ? 1.0f : 0.0f) - m_runBlend) * approachFactor(6.0f, dt);
}

void Player::updateCamera(float dt, const Physics& physics) {
    const float speed = horizontalSpeed();
    const float crouch = crouchFactor();

    // ---- Velocity-linked head bob --------------------------------------------------------
    // The vertical term completes one cycle per pi of phase (= one footstep),
    // so the phase rate is pi * footsteps-per-second.
    if (m_grounded && speed > 0.05f) {
        const float cadence = cfg::kBobCadenceBase + cfg::kBobCadencePerSpeed * speed;
        const float advanced = m_bobPhase + 0.5f * kTwoPi * cadence * dt;

        // A foot strikes each time the phase crosses the bottom of a dip, so
        // footstep sounds stay locked to the visible head motion.
        const int before = static_cast<int>(std::floor((m_bobPhase - kFootStrikePhase) / kPi));
        const int after = static_cast<int>(std::floor((advanced - kFootStrikePhase) / kPi));
        if (after > before && speed > 0.4f) {
            const float intensity = std::clamp(speed / cfg::kRunSpeed, 0.0f, 1.0f) * glm::mix(1.0f, 0.45f, crouch);
            m_events.push_back({PlayerEvent::Type::Footstep, intensity, m_feet.y > kElevatedHeight,
                                (after & 1) ? 1 : -1});
        }
        m_bobPhase = std::fmod(advanced, kTwoPi);
    }
    const float bobTarget = m_grounded ? std::min(speed / cfg::kWalkSpeed, 1.6f) : 0.0f;
    m_bobWeight += (bobTarget - m_bobWeight) * approachFactor(8.0f, dt);
    const float amp = m_bobWeight * glm::mix(1.0f, 0.6f, crouch);
    const float bobY = std::sin(2.0f * m_bobPhase) * cfg::kBobVerticalAmp * amp; // one dip per footstep
    const float bobX = std::cos(m_bobPhase) * cfg::kBobLateralAmp * amp;         // sway per stride

    // ---- Landing dip: critically damped spring, sub-stepped for stability -------------------
    const float k = 170.0f;
    const float c = 2.0f * std::sqrt(k);
    float remaining = dt;
    while (remaining > 0.0f) {
        const float h = std::min(remaining, 1.0f / 240.0f);
        m_landVelocity += (-k * m_landOffset - c * m_landVelocity) * h;
        m_landOffset += m_landVelocity * h;
        remaining -= h;
    }

    // ---- Smooth out step-ups -------------------------------------------------------------
    m_stepOffset *= std::exp(-12.0f * dt);

    glm::vec3 eye = m_feet;
    eye.y += m_height - cfg::kEyeBelowTop + bobY + m_landOffset + m_stepOffset;
    eye += m_camera.right() * bobX;
    eye.y = std::min(eye.y, physics.ceilingY() - 0.06f);

    m_camera.position = eye;
    m_camera.yaw = m_yaw;
    m_camera.pitch = m_pitch;
    m_camera.fovYDegrees = cfg::kFieldOfViewDeg + cfg::kRunFovBoostDeg * m_runBlend;
}
