#include "VulkanContext.h"
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <set>

namespace irox {

void VulkanContext::init(int width, int height, const char* title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error("SDL init failed");
    }

    m_window = SDL_CreateWindow(title, width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    // Instance
    VkApplicationInfo appInfo {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "irox Engine",
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t extCount = 0;
    const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

    VkInstanceCreateInfo instanceInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data()
    };
    vkCreateInstance(&instanceInfo, nullptr, &m_instance);

    SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface);

    // Physical Device
    uint32_t devCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &devCount, nullptr);
    std::vector<VkPhysicalDevice> devices(devCount);
    vkEnumeratePhysicalDevices(m_instance, &devCount, devices.data());
    m_physicalDevice = devices[0];

    createLogicalDevice();
    m_memoryManager.init(m_instance, m_physicalDevice, m_device);

    VkSemaphoreTypeCreateInfo timelineInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0
    };
    VkSemaphoreCreateInfo semInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineInfo };
    vkCreateSemaphore(m_device, &semInfo, nullptr, &m_uploadTimelineSemaphore);

    createSwapChain();
    createDepthResources();
    createRenderPass();
    createFramebuffers();
    createSyncObjects();

    VkCommandPoolCreateInfo uploadPoolInfo {
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = m_graphicsQueueIndex
    };
    vkCreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadCommandPool);

    VkCommandBufferAllocateInfo uploadAllocInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_uploadCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    vkAllocateCommandBuffers(m_device, &uploadAllocInfo, &m_uploadCommandBuffer);

    // Descriptor Pool & Layout
    VkDescriptorSetLayoutBinding binding {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout);

    VkDescriptorPoolSize poolSize { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 200 };
    VkDescriptorPoolCreateInfo poolInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 200,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool);

    // Sampler & Default Fallback Texture
    VkSamplerCreateInfo samplerInfo {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
    vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler);

    VkCommandBuffer cmd = beginSingleTimeCommands();
    createDefaultTexture(cmd);
    endSingleTimeCommands(cmd);

    m_defaultDescriptorSet = createTextureDescriptor(m_defaultTexture.defaultView);
}

bool VulkanContext::isMinimized() const {
    if (!m_window) return true;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);

    SDL_WindowFlags flags = SDL_GetWindowFlags(m_window);

    if (w <= 0 || h <= 0 || (flags & SDL_WINDOW_HIDDEN) || (flags & SDL_WINDOW_MINIMIZED)) {
        return true;
    }
    return false;
}

void VulkanContext::createLogicalDevice() {
    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(qCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, queues.data());

    for (uint32_t i = 0; i < qCount; ++i) {
        if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) m_graphicsQueueIndex = i;
        VkBool32 present = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &present);
        if (present) m_presentQueueIndex = i;
    }

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    std::set<uint32_t> uniqueQueues = { m_graphicsQueueIndex, m_presentQueueIndex };
    for (uint32_t q : uniqueQueues) {
        queueInfos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = q,
            .queueCount = 1,
            .pQueuePriorities = &priority
        });
    }

    std::vector<const char*> devExts = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_timeline_semaphore",
            "VK_KHR_buffer_device_address"
    };

    VkPhysicalDeviceVulkan12Features f12 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .timelineSemaphore = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE
    };

    VkDeviceCreateInfo devInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &f12,
        .queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size()),
        .pQueueCreateInfos = queueInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(devExts.size()),
        .ppEnabledExtensionNames = devExts.data()
    };
    vkCreateDevice(m_physicalDevice, &devInfo, nullptr, &m_device);

    vkGetDeviceQueue(m_device, m_graphicsQueueIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueIndex, 0, &m_presentQueue);
}

bool VulkanContext::createSwapChain() {
    if (isMinimized()) return false;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& fmt : formats) {
        if ((fmt.format == VK_FORMAT_B8G8R8A8_UNORM || fmt.format == VK_FORMAT_R8G8B8A8_UNORM ||
            fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = fmt;
            break;
        }
    }
    m_swapchainFormat = chosenFormat.format;
    VkColorSpaceKHR colorSpace = chosenFormat.colorSpace;

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    if (w <= 0 || h <= 0) return false;

    m_swapchainExtent = {
        std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height)
    };

    VkSurfaceTransformFlagBitsKHR preTransform = caps.currentTransform;
    if (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR scInfo {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_surface,
        .minImageCount = imageCount,
        .imageFormat = m_swapchainFormat,
        .imageColorSpace = colorSpace,
        .imageExtent = m_swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = preTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE
    };

    scInfo.imageSharingMode = (m_graphicsQueueIndex != m_presentQueueIndex)
        ? VK_SHARING_MODE_CONCURRENT
        : VK_SHARING_MODE_EXCLUSIVE;

    uint32_t queueIndices[] = { m_graphicsQueueIndex, m_presentQueueIndex };
    if (scInfo.imageSharingMode == VK_SHARING_MODE_CONCURRENT) {
        scInfo.queueFamilyIndexCount = 2;
        scInfo.pQueueFamilyIndices = queueIndices;
    }

    if (vkCreateSwapchainKHR(m_device, &scInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        return false;
    }

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_swapchainFormat,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]);
    }
    return true;
}

void VulkanContext::createDepthResources() {
    m_depthFormat = VK_FORMAT_D32_SFLOAT;
    VkImageCreateInfo depthInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = m_depthFormat,
        .extent = { m_swapchainExtent.width, m_swapchainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
    };
    m_depthImage = m_memoryManager.createImage(depthInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VK_IMAGE_ASPECT_DEPTH_BIT);
}

void VulkanContext::createRenderPass() {
    VkAttachmentDescription colorAttachment {
        .format = m_swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentDescription depthAttachment {
        .format = m_depthFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference colorRef { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef { .attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
        .pDepthStencilAttachment = &depthRef
    };

    VkSubpassDependency dep {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
    VkRenderPassCreateInfo rpInfo {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dep
    };
    vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass);
}

void VulkanContext::createFramebuffers() {
    m_framebuffers.resize(m_swapchainImageViews.size());
    for (size_t i = 0; i < m_swapchainImageViews.size(); ++i) {
        std::array<VkImageView, 2> atts = { m_swapchainImageViews[i], m_depthImage.defaultView };
        VkFramebufferCreateInfo fbInfo {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = m_renderPass,
            .attachmentCount = static_cast<uint32_t>(atts.size()),
            .pAttachments = atts.data(),
            .width = m_swapchainExtent.width,
            .height = m_swapchainExtent.height,
            .layers = 1
        };
        vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffers[i]);
    }
}

void VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        // Command Pool на каждый кадр
        VkCommandPoolCreateInfo poolInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_graphicsQueueIndex
        };
        vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_frames[i].commandPool);

        VkCommandBufferAllocateInfo allocInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_frames[i].commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        vkAllocateCommandBuffers(m_device, &allocInfo, &m_frames[i].commandBuffer);

        vkCreateSemaphore(m_device, &semInfo, nullptr, &m_frames[i].imageAvailableSemaphore);
        vkCreateSemaphore(m_device, &semInfo, nullptr, &m_frames[i].renderFinishedSemaphore);
        vkCreateFence(m_device, &fenceInfo, nullptr, &m_frames[i].inFlightFence);
    }
}


bool VulkanContext::beginFrame(uint32_t& outImageIndex) {
    if (isMinimized()) return false;

    if (m_framebufferResized) {
        recreateSwapchain();
        if (isMinimized() || m_swapchain == VK_NULL_HANDLE) return false;
    }

    if (m_swapchain == VK_NULL_HANDLE) return false;

    auto& currentFrame = m_frames[m_currentFrameIndex];
    vkWaitForFences(m_device, 1, &currentFrame.inFlightFence, VK_TRUE, UINT64_MAX);

    VkResult res = vkAcquireNextImageKHR(
        m_device, m_swapchain, UINT64_MAX,
        currentFrame.imageAvailableSemaphore, VK_NULL_HANDLE, &outImageIndex
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || res == VK_ERROR_SURFACE_LOST_KHR) {
        m_framebufferResized = true;
        return false;
    }

    vkResetFences(m_device, 1, &currentFrame.inFlightFence);
    return true;
}

void VulkanContext::endFrame(uint32_t imageIndex) {
    auto& currentFrame = m_frames[m_currentFrameIndex];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &currentFrame.imageAvailableSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &currentFrame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &currentFrame.renderFinishedSemaphore
    };

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, currentFrame.inFlightFence);

    VkPresentInfoKHR presentInfo {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &currentFrame.renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &imageIndex
    };

    VkResult res = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        m_framebufferResized = true;
    }

    m_currentFrameIndex = (m_currentFrameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanContext::recordFrame(uint32_t imageIndex, const std::function<void(VkCommandBuffer)>& recordFn) {
    auto& currentFrame = m_frames[m_currentFrameIndex];
    vkResetCommandBuffer(currentFrame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(currentFrame.commandBuffer, &beginInfo);

    std::array<VkClearValue, 2> clears {};
    clears[0].color = { { 0.25f, 0.05f, 0.08f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpBegin {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_renderPass,
        .framebuffer = m_framebuffers[imageIndex],
        .renderArea = {.offset = {0, 0}, .extent = m_swapchainExtent },
        .clearValueCount = static_cast<uint32_t>(clears.size()),
        .pClearValues = clears.data()
    };

    vkCmdBeginRenderPass(currentFrame.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(m_swapchainExtent.width),
        .height = static_cast<float>(m_swapchainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(currentFrame.commandBuffer, 0, 1, &vp);

    VkRect2D sc {
        .offset = {0, 0},
        .extent = m_swapchainExtent
    };
    vkCmdSetScissor(currentFrame.commandBuffer, 0, 1, &sc);

    recordFn(currentFrame.commandBuffer);

    vkCmdEndRenderPass(currentFrame.commandBuffer);
    vkEndCommandBuffer(currentFrame.commandBuffer);
}

void VulkanContext::cleanupSwapchain() {
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_framebuffers.clear();

    m_memoryManager.destroyImage(m_depthImage);

    for (auto v : m_swapchainImageViews) vkDestroyImageView(m_device, v, nullptr);
    m_swapchainImageViews.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanContext::recreateSwapchain() {
    if (isMinimized()) return;

    vkDeviceWaitIdle(m_device);
    cleanupSwapchain();

    recreateSurface();
    \
    m_framebufferResized = false;

    if (createSwapChain()) {
        createDepthResources();
        createFramebuffers();
        SDL_Log("[Vulkan] Swapchain successfully recreated!");
    } else {
        SDL_Log("[Vulkan] createSwapChain failed, retrying next frame");
        m_framebufferResized = true;
    }
}

VkDescriptorSet VulkanContext::createTextureDescriptor(VkImageView imageView) {
    VkDescriptorSetAllocateInfo allocInfo {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_descriptorSetLayout
    };
    VkDescriptorSet ds = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_device, &allocInfo, &ds);

    VkDescriptorImageInfo imgInfo {
        .sampler = m_textureSampler,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = ds,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imgInfo
    };
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    return ds;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
    vkResetCommandBuffer(m_uploadCommandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(m_uploadCommandBuffer, &beginInfo);
    return m_uploadCommandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkTimelineSemaphoreSubmitInfo timelineInfo {
        .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
        .signalSemaphoreValueCount = 1,
        .pSignalSemaphoreValues = &m_uploadTimelineValue,
    };

    VkSubmitInfo submit {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = (m_uploadTimelineSemaphore != VK_NULL_HANDLE) ? &timelineInfo : nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
        .signalSemaphoreCount = (m_uploadTimelineSemaphore != VK_NULL_HANDLE) ? 1u : 0u,
        .pSignalSemaphores = (m_uploadTimelineSemaphore != VK_NULL_HANDLE) ? &m_uploadTimelineSemaphore : nullptr,
    };

    vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_graphicsQueue);
}

void VulkanContext::shutdown() {
    vkDeviceWaitIdle(m_device);

    for (auto& frame : m_frames) {
        vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(m_device, frame.renderFinishedSemaphore, nullptr);
        vkDestroyFence(m_device, frame.inFlightFence, nullptr);
        vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
    }

    if (m_uploadCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_uploadCommandPool, nullptr);
        m_uploadCommandPool = VK_NULL_HANDLE;
    }

    cleanupSwapchain();

    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    vkDestroySampler(m_device, m_textureSampler, nullptr);
    m_memoryManager.destroyImage(m_defaultTexture);
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);

    if (m_uploadTimelineSemaphore) vkDestroySemaphore(m_device, m_uploadTimelineSemaphore, nullptr);
    m_memoryManager.shutdown();

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    SDL_DestroyWindow(m_window);
    SDL_Quit();
}

void VulkanContext::createDefaultTexture(VkCommandBuffer cmd) {
    const uint32_t W = 16, H = 16;
    std::vector<uint32_t> pixels(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            bool even = ((x / 2) + (y / 2)) % 2 == 0;
            pixels[y * W + x] = even ? 0xFF202020 : 0xFFEE10EE;
        }
    }

    VkImageCreateInfo imgInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { W, H, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    m_defaultTexture = m_memoryManager.createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    m_memoryManager.sendStagingImageData(
        cmd, m_uploadTimelineSemaphore, ++m_uploadTimelineValue,
        m_defaultTexture.image, { W, H, 1 }, pixels.data(), pixels.size() * sizeof(uint32_t)
    );
}

void VulkanContext::recreateSurface() {
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface);
}

} // namespace irox