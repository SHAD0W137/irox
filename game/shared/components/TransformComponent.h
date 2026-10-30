#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace irox {
    struct TransformComponent {
        glm::vec3 position = { 0.0f, 0.0f, 0.0f };
        glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 scale = { 1.0f, 1.0f, 1.0f };

        glm::mat4 getMatrix() const {
            return glm::translate(glm::mat4(1.0f), position) *
                glm::mat4_cast(rotation) *
                glm::scale(glm::mat4(1.0f), scale);
        }
    };
}