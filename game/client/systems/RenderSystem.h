#pragma once
#include "engine/ecs/ECS.h"
#include <vulkan/vulkan.h>

namespace irox {
class RenderSystem {
public:
    static void recordCommands(
        ecs::World& world,
        VkCommandBuffer cmd,
        VkPipelineLayout layout,
        VkPipeline pipeline,
        ecs::Entity cameraEntity
    );
};
}