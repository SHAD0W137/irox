#pragma once
#include "engine/render/GPUMemoryManager.h"
#include <string>
#include <vector>
#include <cstdint>

namespace irox {

class KtxTextureLoader {
public:
    static bool loadFromFile(
        const std::string& filePath,
        irox::GPUMemoryManager& memoryManager,
        VkCommandBuffer transferCmd,
        VkSemaphore timelineSemaphore,
        uint64_t& timelineValue,
        irox::AllocatedImage& outImage
    );

    static bool loadFromMemory(
        const uint8_t* data,
        size_t size,
        irox::GPUMemoryManager& memoryManager,
        VkCommandBuffer transferCmd,
        VkSemaphore timelineSemaphore,
        uint64_t& timelineValue,
        irox::AllocatedImage& outImage
    );

    static irox::AllocatedImage createFallbackTexture(
        irox::GPUMemoryManager& memoryManager,
        VkCommandBuffer transferCmd,
        VkSemaphore timelineSemaphore,
        uint64_t& timelineValue
    );
};

} // namespace irox