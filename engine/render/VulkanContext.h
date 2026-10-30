#pragma once
#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>
#include <array>
#include "GPUMemoryManager.h"
#include "RenderTypes.h"

namespace irox {

struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

class VulkanContext {
public:
    void init(int width, int height, const char* title);
    void shutdown();

    bool beginFrame(uint32_t& outImageIndex);
    void endFrame(uint32_t imageIndex);

    void recordFrame(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& recordFn);

    bool isMinimized() const;
    void onWindowResize() { m_framebufferResized = true; }

    float getAspectRatio() const {
        if (m_swapchainExtent.height == 0) return 1.0f;
        return static_cast<float>(m_swapchainExtent.width) / static_cast<float>(m_swapchainExtent.height);
    }

    void waitIdle() { vkDeviceWaitIdle(m_device); }

    // Core
    VkDevice getDevice() const { return m_device; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkExtent2D getSwapchainExtent() const { return m_swapchainExtent; }
    irox::GPUMemoryManager& getMemoryManager() { return m_memoryManager; }

    // Descriptors and textures
    VkDescriptorSet getDefaultDescriptorSet() const { return m_defaultDescriptorSet; }
    irox::AllocatedImage getDefaultTexture() const { return m_defaultTexture; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
    VkDescriptorSet createTextureDescriptor(VkImageView imageView);

    // Staging buffer
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);
    VkSemaphore getUploadTimelineSemaphore() const { return m_uploadTimelineSemaphore; }
    uint64_t& getUploadTimelineValue() { return m_uploadTimelineValue; }

private:
    VkCommandPool m_uploadCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_uploadCommandBuffer = VK_NULL_HANDLE;

    SDL_Window* m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueIndex = UINT32_MAX;
    uint32_t m_presentQueueIndex = UINT32_MAX;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapchainFormat {};
    VkExtent2D m_swapchainExtent { 0, 0 };
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;

    irox::AllocatedImage m_depthImage;
    VkFormat m_depthFormat {};

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    uint32_t m_currentFrameIndex = 0;
    bool m_framebufferResized = false;

    irox::GPUMemoryManager m_memoryManager;
    VkSemaphore m_uploadTimelineSemaphore = VK_NULL_HANDLE;
    uint64_t m_uploadTimelineValue = 0;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_textureSampler = VK_NULL_HANDLE;
    irox::AllocatedImage m_defaultTexture;
    VkDescriptorSet m_defaultDescriptorSet = VK_NULL_HANDLE;

    void createLogicalDevice();
    bool createSwapChain();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void createSyncObjects();
    void recreateSwapchain();
    void cleanupSwapchain();
    void createDefaultTexture(VkCommandBuffer cmd);
    void recreateSurface();
};

} // namespace irox