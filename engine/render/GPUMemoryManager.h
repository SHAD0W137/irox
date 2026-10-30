#pragma once

#include <vulkan/vulkan.h>

#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#elif __has_include(<vk_mem_alloc.h>)
#include <vk_mem_alloc.h>
#else
#error "vk_mem_alloc.h not found!"
#endif

#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>

#include <iostream>
#define VK_CHECK(f) {                                                                           \
    VkResult res = (f);                                                                         \
    if (res != VK_SUCCESS) {                                                                    \
        std::cerr << "Fatal: VkResult is " << (int)res << " in " << __FILE__ << " at line " << __LINE__ << "\n"; \
        std::exit(-1);                                                                          \
    }                                                                                           \
}

/// <summary>
/// Supporting class to handle Vulkan GPU memory allocations. VMA library is used to handle the allocation.
/// </summary>

namespace irox {

// Default Buffer
struct AllocatedData {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkDeviceAddress deviceAddress = 0;
};

// Image Buffer
struct AllocatedImage {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView defaultView = VK_NULL_HANDLE;
};

// Mesh in Megabuffer
struct MeshAllocation {
	uint32_t chunkIndex = 0;
	VkBuffer allocated = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	VkDeviceAddress deviceAddress = 0;
	VmaVirtualAllocation virtualAlloc = VK_NULL_HANDLE;
	VkDeviceSize size = 0;
};

// Buffer for dynamically changing data
// Used for Matrix buffer and Staging buffer
struct HostMappedBuffer {
	AllocatedData allocated;
	void* mappedData = nullptr;
	VkDeviceSize size = 0;
};


class GPUMemoryManager {
public:
	GPUMemoryManager() = default;
	~GPUMemoryManager() = default;
	GPUMemoryManager(const GPUMemoryManager&) = delete;
	GPUMemoryManager& operator=(const GPUMemoryManager&) = delete;

	void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
	void shutdown();

	AllocatedData createBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VmaMemoryUsage memoryUsage,
		VmaAllocationCreateFlags flags = 0
	);
	void destroyBuffer(AllocatedData& allocated);

	AllocatedImage createImage(
		const VkImageCreateInfo& imageInfo,
		VmaMemoryUsage memoryUsage,
		VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT
	);
	void destroyImage(AllocatedImage& image);

	MeshAllocation allocateGeometry(VkDeviceSize sizeInBytes, VkDeviceSize alignment = 16);
	void freeGeometry(const MeshAllocation& alloc);


	VmaAllocator getAllocator() const { return m_allocator; }

	HostMappedBuffer createHostMappedBuffer(
		VkDeviceSize size,
		VkBufferUsageFlags usage
	);

	void destroyHostMappedBuffer(HostMappedBuffer& allocated);

	void sendStagingData(
		VkCommandBuffer transferCmd,
		VkSemaphore timelineSemaphore,
		uint64_t currentTimelineSubmissionValue,
		VkBuffer dstBuffer,
		VkDeviceSize dstOffset,
		const void* srcData,
		VkDeviceSize sizeInBytes,
		VkDeviceSize alignment = 16
	);

	void sendStagingImageData(
		VkCommandBuffer transferCmd,
		VkSemaphore timelineSemaphore,
		uint64_t currentTimelineSubmissionValue,
		VkImage dstImage,
		VkExtent3D extent,
		const void* srcData,
		VkDeviceSize sizeInBytes,
		VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
	);

private:
	struct MegaBufferChunk {
		VkBuffer allocated = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkDeviceAddress deviceAddress = 0;
		VmaVirtualBlock virtualBlock = VK_NULL_HANDLE;
		VkDeviceSize capacity = 0;
	};

	struct StagingAllocationToken {
		VmaVirtualAllocation virtualAlloc;
		uint64_t timelineWaitValue;
	};

	struct StagingRingBuffer {
		HostMappedBuffer memory;
		VmaVirtualBlock virtualBlock = VK_NULL_HANDLE;
		const VkDeviceSize totalSize = 64 * 1024 * 1024; // 64 MiB
		std::deque<StagingAllocationToken> inFlightAllocations;
	};

	uint32_t createMegaBufferChunk(VkDeviceSize size);
	void retireCompletedStagingSlices(VkSemaphore timelineSemaphore);

private:
	VkInstance m_instance = VK_NULL_HANDLE;
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkAllocationCallbacks* m_vkAllocator = nullptr;

	// Subsystems
	StagingRingBuffer m_staging;
	std::vector<MegaBufferChunk> m_megaBufferChunks;
	const VkDeviceSize m_defaultChunkSize = 256 * 1024 * 1024; // 256 MiB

	std::mutex m_megaBufferMutex;
	std::mutex m_stagingMutex;
};

} // namespace irox