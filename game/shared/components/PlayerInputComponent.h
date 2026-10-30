#pragma once
#include <glm/glm.hpp>

namespace irox {

struct PlayerInputComponent {
    glm::vec3 moveDir { 0.0f };
    bool jumpRequested = false;
    float subtickJumpFraction = 0.0f;
};

} // namespace irox