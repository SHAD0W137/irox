#include "game/shared/systems/PhysicsSystem.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CapsuleColliderComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include <cmath>

namespace irox {

void PhysicsSystem::update(ecs::World& world, float fixedDt) {
    auto* physics = world.GetSingleton<physics::PhysicsWorldSingleton>();
    if (!physics) return;

    // Step Box3D physics with 4 sub-steps per tick
    physics->step(fixedDt, 4);

    auto view = world.View<RigidBodyComponent, TransformComponent>();

    for (ecs::Entity entity : view) {
        auto& rb = view.get<RigidBodyComponent>(entity);
        if (!b3Body_IsValid(rb.bodyId)) continue;

        auto& tf = view.get<TransformComponent>(entity);

        b3Vec3 pos = b3Body_GetPosition(rb.bodyId);
        b3Quat rot = b3Body_GetRotation(rb.bodyId);

        tf.position = glm::vec3(pos.x, pos.y, pos.z);
        tf.rotation = glm::quat(rot.s, rot.v.x, rot.v.y, rot.v.z);

        // Update grounded state for Character Controllers
        if (world.hasComponent<CharacterControllerComponent>(entity) &&
            world.hasComponent<CapsuleColliderComponent>(entity)) {

            auto& cc = world.getComponent<CharacterControllerComponent>(entity);
            auto& col = world.getComponent<CapsuleColliderComponent>(entity);

            b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);
            cc.velocity = glm::vec3(vel.x, vel.y, vel.z);

            // Distance from capsule bottom hemisphere to floor surface (Y = 0)
            const float footY = tf.position.y - (col.halfHeight + col.radius);

            cc.isGrounded = (footY <= 0.05f) && (vel.y <= 0.1f);
        }
    }
}

} // namespace irox