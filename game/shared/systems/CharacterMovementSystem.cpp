#include "game/shared/systems/CharacterMovementSystem.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/PlayerInputComponent.h"

namespace irox {

void CharacterMovementSystem::step(
    RigidBodyComponent& rb,
    CharacterControllerComponent& controller,
    const glm::vec3& moveDir,
    bool jumpRequested
) {
    if (!b3Body_IsValid(rb.bodyId)) return;

    b3Vec3 currentVel = b3Body_GetLinearVelocity(rb.bodyId);

    float targetVelX = moveDir.x * controller.moveSpeed;
    float targetVelZ = moveDir.z * controller.moveSpeed;

    float targetVelY = currentVel.y;
    if (controller.isGrounded && jumpRequested) {
        targetVelY = controller.jumpSpeed;
        controller.isGrounded = false;
    }

    b3Body_SetLinearVelocity(rb.bodyId, b3Vec3 { targetVelX, targetVelY, targetVelZ });
}

void CharacterMovementSystem::update(ecs::World& world, float fixedDt) {
    auto& controllerPool = world.getPool<CharacterControllerComponent>();

    for (ecs::Entity entity : controllerPool.entities()) {
        if (!world.hasComponent<RigidBodyComponent>(entity) ||
            !world.hasComponent<PlayerInputComponent>(entity)) {
            continue;
        }

        auto& rb = world.getComponent<RigidBodyComponent>(entity);
        auto& controller = controllerPool.get(entity);
        auto& playerInput = world.getComponent<PlayerInputComponent>(entity);

        step(rb, controller, playerInput.moveDir, playerInput.jumpRequested);
        playerInput.jumpRequested = false;
    }
}

} // namespace irox