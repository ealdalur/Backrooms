// ---------------------------------------------------------------------------
// Camera.cpp
// ---------------------------------------------------------------------------
#include "Render/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

glm::vec3 Camera::forward() const {
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3(-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp));
}

glm::vec3 Camera::flatForward() const {
    return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
}

glm::vec3 Camera::right() const {
    return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projectionMatrix(float aspect) const {
    return glm::perspective(glm::radians(fovYDegrees), aspect, nearPlane, farPlane);
}
