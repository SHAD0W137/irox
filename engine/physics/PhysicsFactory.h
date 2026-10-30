#pragma once
#include "engine/physics/PhysicsWorld.h"
#include "game/shared/components/RigidBodyComponent.h"
#include <glm/glm.hpp>

namespace irox::physics {

class PhysicsFactory {
public:
    static RigidBodyComponent createStaticGroundBox(
        b3WorldId worldId,
        const glm::vec3& position,
        const glm::vec3& halfExtents
    ) {
        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type = b3_staticBody;
        bodyDef.position = b3Vec3 { position.x, position.y, position.z };

        b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);

        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.baseMaterial.friction = 0.5f;

        b3BoxHull boxHull = b3MakeBoxHull(halfExtents.x, halfExtents.y, halfExtents.z);
        b3ShapeId shapeId = b3CreateHullShape(bodyId, &shapeDef, &boxHull.base);

        return RigidBodyComponent {
            .bodyId = bodyId,
            .shapeId = shapeId,
            .type = BodyType::Static
        };
    }

    static RigidBodyComponent createCharacterCapsule(
        b3WorldId worldId,
        const glm::vec3& position,
        float radius,
        float halfHeight
    ) {
        b3BodyDef bodyDef = b3DefaultBodyDef();
        bodyDef.type = b3_dynamicBody; // Calculating jump via physics engine
        bodyDef.position = b3Vec3 { position.x, position.y, position.z };

        // All rotations are frozen
        bodyDef.motionLocks.angularX = true;
        bodyDef.motionLocks.angularY = true;
        bodyDef.motionLocks.angularZ = true;

        b3BodyId bodyId = b3CreateBody(worldId, &bodyDef);

        b3Capsule capsule;
        capsule.center1 = b3Vec3 { 0.0f, -halfHeight, 0.0f };
        capsule.center2 = b3Vec3 { 0.0f,  halfHeight, 0.0f };
        capsule.radius = radius;

        b3ShapeDef shapeDef = b3DefaultShapeDef();
        shapeDef.density = 1.0f;
        shapeDef.baseMaterial.friction = 0.1f; 

        b3ShapeId shapeId = b3CreateCapsuleShape(bodyId, &shapeDef, &capsule);

        return RigidBodyComponent {
            .bodyId = bodyId,
            .shapeId = shapeId,
            .type = BodyType::Dynamic
        };
    }
};

} // namespace irox::physics