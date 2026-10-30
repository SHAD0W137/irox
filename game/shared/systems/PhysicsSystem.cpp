#include "game/shared/systems/PhysicsSystem.h"
#include "engine/physics/PhysicsWorld.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CapsuleColliderComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include <cmath>

namespace irox {

void PhysicsSystem::update(ecs::World& world, float fixedDt) {
    if (!world.hasSingleton<physics::PhysicsWorldSingleton>()) return;

    auto& physics = world.getSingleton<physics::PhysicsWorldSingleton>();
    physics.step(fixedDt, 4);

    auto& bodyPool = world.getPool<RigidBodyComponent>();

    for (ecs::Entity entity : bodyPool.entities()) {
        if (!world.hasComponent<TransformComponent>(entity)) continue;

        auto& rb = bodyPool.get(entity);
        if (!b3Body_IsValid(rb.bodyId)) continue;

        auto& tf = world.getComponent<TransformComponent>(entity);

        b3Vec3 pos = b3Body_GetPosition(rb.bodyId);
        b3Quat rot = b3Body_GetRotation(rb.bodyId);
        // Box3D to glm conversion
        tf.position = glm::vec3(pos.x, pos.y, pos.z);
        tf.rotation = glm::quat(rot.s, rot.v.x, rot.v.y, rot.v.z);

        if (world.hasComponent<CharacterControllerComponent>(entity) &&
            world.hasComponent<CapsuleColliderComponent>(entity)) {
            auto& cc = world.getComponent<CharacterControllerComponent>(entity);
            auto& col = world.getComponent<CapsuleColliderComponent>(entity);

            b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);
            cc.velocity = glm::vec3(vel.x, vel.y, vel.z);

            float bottomY = tf.position.y - col.halfHeight - col.radius;
            cc.isGrounded = (bottomY <= 0.05f) && (std::abs(vel.y) < 0.2f);
        }
    }
}

} // namespace irox