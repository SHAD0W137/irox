#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace irox {

struct CameraComponent {
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float distance = 6.0f;
    glm::vec3 target { 0.0f, 1.0f, 0.0f };

    float yaw = 0.0f;
    float pitch = 0.35f;

    glm::quat orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::mat4 viewMatrix { 1.0f };
    glm::mat4 projMatrix { 1.0f };
};

} // namespace irox