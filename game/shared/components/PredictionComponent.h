#pragma once
#include <cstdint>
#include <array>
#include <glm/glm.hpp>

namespace irox {

struct PredictedState {
    uint32_t tick = 0;
    glm::vec3 position { 0.0f };
    glm::vec3 velocity { 0.0f };
    bool isGrounded = false;
};

struct PredictedInput {
    uint32_t tick = 0;
    glm::vec3 moveDir { 0.0f };
    bool jumpRequested = false;
};

struct LocalPlayerPredictionComponent {
    static constexpr size_t HISTORY_CAPACITY = 128; // 128 tick history (~2.13 sec at 60Hz)

    std::array<PredictedInput, HISTORY_CAPACITY> inputHistory {};
    std::array<PredictedState, HISTORY_CAPACITY> stateHistory {};

    uint32_t currentClientTick = 0;
    uint32_t lastAcknowledgedServerTick = 0;

    glm::vec3 visualErrorOffset { 0.0f };
};

} // namespace irox