#pragma once
#include "engine/ecs/ECS.h"

namespace irox {

class PhysicsSystem {
public:
    // Fixed 60hz simulation
    static void update(ecs::World& world, float fixedDt);
};

} // namespace irox