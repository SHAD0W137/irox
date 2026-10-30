#pragma once
#include <glm/glm.hpp>

namespace irox {

struct CharacterControllerComponent {
    glm::vec3 velocity { 0.0f };
    float moveSpeed = 7.0f;
    float jumpSpeed = 6.5f;
    float gravity = -18.0f;
    bool isGrounded = false;
};

} // namespace irox