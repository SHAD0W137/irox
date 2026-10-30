#pragma once
#include "engine/ecs/ECS.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include <glm/glm.hpp>

namespace irox {

class CharacterMovementSystem {
public:
    // Step simulation fucntion for rollback
    static void step(
        RigidBodyComponent& rb,
        CharacterControllerComponent& controller,
        const glm::vec3& moveDir,
        bool jumpRequested
    );

    // Standard ECS step
    static void update(ecs::World& world, float fixedDt);
};

} // namespace irox