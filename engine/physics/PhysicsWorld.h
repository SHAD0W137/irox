#pragma once
#include <box3d/box3d.h>
#include <box3d/math_functions.h>
#include <iostream>

namespace irox::physics {

struct PhysicsWorldSingleton {
    b3WorldId worldId = b3_nullWorldId;

    void init(float gravityY = -18.0f) {
        b3WorldDef worldDef = b3DefaultWorldDef();
        worldDef.gravity = b3Vec3 { 0.0f, gravityY, 0.0f };

        this->worldId = b3CreateWorld(&worldDef);
        std::cout << "[Box3D] Physics World initialized.\n";
    }

    void step(float timeStep, int subStepCount = 4) {
        if (b3World_IsValid(worldId)) {
            b3World_Step(worldId, timeStep, subStepCount);
        }
    }

    void shutdown() {
        if (b3World_IsValid(worldId)) {
            b3DestroyWorld(worldId);
            worldId = b3_nullWorldId;
            std::cout << "[Box3D] Physics World destroyed.\n";
        }
    }
};

} // namespace irox::physics