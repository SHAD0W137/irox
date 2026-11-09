#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace irox {

struct alignas(16) TransformComponent {
    glm::vec3 position { 0.0f, 0.0f, 0.0f };
    float _pad0 { 0.0f }; // Align rotation to 16-byte boundary for SIMD

    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f }; // w, x, y, z

    glm::vec3 scale { 1.0f, 1.0f, 1.0f };
    float _pad1 { 0.0f };

    [[nodiscard]] glm::mat4 getMatrix() const noexcept {
        return glm::translate(glm::mat4(1.0f), position) *
            glm::mat4_cast(rotation) *
            glm::scale(glm::mat4(1.0f), scale);
    }
};

} // namespace irox