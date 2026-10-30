#pragma once
#include "engine/ecs/ECS.h"

namespace irox {
class CameraSystem {
public:
    static void update(ecs::World& world, float aspectRatio);
};
}