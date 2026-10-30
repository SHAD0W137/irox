#pragma once
#include "engine/render/GPUMemoryManager.h"
#include "engine/render/RenderTypes.h"
#include <vector>
#include <vulkan/vulkan.h>

namespace irox {

struct MeshComponent {
    irox::MeshAllocation vertexAlloc;
    irox::MeshAllocation indexAlloc;
    std::vector<Primitive> primitives;
    VkDescriptorSet defaultDescriptorSet = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> materialDescriptorSets;
};

} // namespace irox