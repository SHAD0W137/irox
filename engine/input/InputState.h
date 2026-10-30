#pragma once
#include <glm/glm.hpp>

namespace irox {

struct InputState {
    // Mouse
    glm::vec2 mouseDelta { 0.0f, 0.0f };
    float mouseWheel = 0.0f;
    bool isLeftMouseDown = false;
    bool isRightMouseDown = false;

    // Keyboard
    bool keyW = false;
    bool keyA = false;
    bool keyS = false;
    bool keyD = false;
    bool keySpace = false;
    bool keyShift = false;
};

} // namespace irox