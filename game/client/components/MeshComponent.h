#pragma once
#include "engine/render/GPUMemoryManager.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <type_traits>

namespace irox {

struct MeshComponent {
    // MegaBuffer Sub-Allocations
    MeshAllocation vertexAlloc {};
    MeshAllocation indexAlloc {};

    // Direct draw parameters (replaces std::vector<Primitive>)
    uint32_t firstIndex { 0 };
    uint32_t indexCount { 0 };
    int32_t  vertexOffset { 0 };

    // Material descriptor set
    VkDescriptorSet defaultDescriptorSet { VK_NULL_HANDLE };
};

// POD assertion
static_assert(std::is_trivially_copyable_v<MeshComponent>,
    "CRITICAL: MeshComponent must be Trivially Copyable!");

} // namespace irox