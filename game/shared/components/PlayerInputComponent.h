#pragma once
#include <glm/glm.hpp>

namespace irox {

struct PlayerInputComponent {
    glm::vec3 moveDir { 0.0f };
    float subtickJumpFraction { 0.0f };
    bool jumpRequested { false };
    uint8_t _pad[3] { 0, 0, 0 }; // Explicit padding to match 4-byte boundaries
};

} // namespace irox