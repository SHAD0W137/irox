#pragma once
#include "engine/ecs/ECS.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include <glm/glm.hpp>

namespace irox {

class CharacterMovementSystem {
public:
    // Standard 60 Hz physics step
    static void step(
        RigidBodyComponent& rb,
        CharacterControllerComponent& controller,
        const glm::vec3& moveDir,
        bool jumpRequested
    );

    // Overwatch catch-up displacement for inputs clumped during lag spikes
    static void catchUpStep(
        RigidBodyComponent& rb,
        CharacterControllerComponent& controller,
        const glm::vec3& moveDir,
        bool jumpRequested,
        float dt
    );

    static void update(ecs::World& world, float fixedDt);
};

} // namespace irox