#pragma once
// ---------------------------------------------------------------------------
// Camera.h
// First-person perspective camera described by an eye position and
// yaw/pitch Euler angles (radians). Yaw 0 looks down -Z; positive yaw turns
// left (counter-clockwise seen from above).
// ---------------------------------------------------------------------------

#include <glm/glm.hpp>

class Camera {
public:
    glm::vec3 position{0.0f};
    float     yaw   = 0.0f;
    float     pitch = 0.0f;
    float     fovYDegrees = 70.0f;
    float     nearPlane = 0.05f;
    float     farPlane  = 200.0f;

    /// Unit view direction.
    glm::vec3 forward() const;
    /// Unit horizontal forward (pitch ignored), used for movement.
    glm::vec3 flatForward() const;
    /// Unit horizontal right vector.
    glm::vec3 right() const;

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
};
