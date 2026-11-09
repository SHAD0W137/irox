#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <type_traits>

namespace irox {

struct alignas(16) CameraComponent {
    float fov { 45.0f };
    float nearPlane { 0.1f };
    float farPlane { 100.0f };
    float distance { 6.0f };
    glm::vec3 target { 0.0f, 1.0f, 0.0f };

    float yaw { 0.0f };
    float pitch { 0.35f };

    // Restored: Orientation quaternion for view matrix calculation
    glm::quat orientation { 1.0f, 0.0f, 0.0f, 0.0f };

    glm::mat4 viewMatrix { 1.0f };
    glm::mat4 projMatrix { 1.0f };
};

static_assert(std::is_trivially_copyable_v<CameraComponent>,
    "CRITICAL: CameraComponent must be Trivially Copyable!");

} // namespace irox