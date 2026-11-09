#include "game/shared/systems/CharacterMovementSystem.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/PlayerInputComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"

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
    auto view = world.View<CharacterControllerComponent, RigidBodyComponent, PlayerInputComponent>();

    for (ecs::Entity entity : view) {
        auto& controller = view.get<CharacterControllerComponent>(entity);
        auto& rb = view.get<RigidBodyComponent>(entity);
        auto& playerInput = view.get<PlayerInputComponent>(entity);

        step(rb, controller, playerInput.moveDir, playerInput.jumpRequested);
        playerInput.jumpRequested = false;
    }
}

void CharacterMovementSystem::catchUpStep(
    RigidBodyComponent& rb,
    CharacterControllerComponent& controller,
    const glm::vec3& moveDir,
    bool jumpRequested,
    float dt
) {
    if (!b3Body_IsValid(rb.bodyId)) return;

    b3Vec3 pos = b3Body_GetPosition(rb.bodyId);
    b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);

    float targetVelX = moveDir.x * controller.moveSpeed;
    float targetVelZ = moveDir.z * controller.moveSpeed;
    float targetVelY = vel.y;

    if (controller.isGrounded && jumpRequested) {
        targetVelY = controller.jumpSpeed;
        controller.isGrounded = false;
    }

    // Displace position for the missed tick that was held in the buffer
    pos.x += targetVelX * dt;
    pos.z += targetVelZ * dt;
    pos.y += targetVelY * dt;

    // Simple ground constraint check proxy
    if (pos.y <= 0.9f) {
        pos.y = 0.9f;
        controller.isGrounded = true;
        targetVelY = 0.0f;
    }

    b3Body_SetTransform(rb.bodyId, pos, b3Body_GetRotation(rb.bodyId));
    b3Body_SetLinearVelocity(rb.bodyId, b3Vec3 { targetVelX, targetVelY, targetVelZ });
}

} // namespace irox