#pragma once
#include <cstdint>
#include <array>
#include <glm/glm.hpp>
#include "engine/network/NetworkPackets.h"

namespace irox {

struct PredictedState {
    uint32_t tick = 0;
    glm::vec3 position { 0.0f };
    glm::vec3 velocity { 0.0f };
    bool isGrounded = false;
};

struct LocalPlayerPredictionComponent {
    static constexpr size_t HISTORY_CAPACITY = 128; // ~2.13 seconds at 60 Hz

    std::array<net::UserCmd, HISTORY_CAPACITY> inputHistory {};
    std::array<PredictedState, HISTORY_CAPACITY> stateHistory {};

    uint32_t currentClientTick = 0;
    uint32_t lastAcknowledgedServerTick = 0;

    glm::vec3 visualErrorOffset { 0.0f };
};

static_assert(std::is_trivially_copyable_v<LocalPlayerPredictionComponent>,
    "Prediction component must be trivially copyable for rollback!");

} // namespace irox