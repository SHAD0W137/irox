#include "GPUMemoryManager.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>

// Dynamic VMA linking for Android
#if defined(IROX_PLATFORM_ANDROID)
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif

#define VMA_IMPLEMENTATION

#if __has_include(<vma/vk_mem_alloc.h>)
#include <vma/vk_mem_alloc.h>
#elif __has_include(<vk_mem_alloc.h>)
#include <vk_mem_alloc.h>
#endif

#if defined(IROX_PLATFORM_ANDROID)
static PFN_vkGetBufferDeviceAddress pfn_vkGetBufferDeviceAddress = nullptr;
static PFN_vkGetSemaphoreCounterValue pfn_vkGetSemaphoreCounterValue = nullptr;
static PFN_vkWaitSemaphores pfn_vkWaitSemaphores = nullptr;

#define vkGetBufferDeviceAddress pfn_vkGetBufferDeviceAddress
#define vkGetSemaphoreCounterValue pfn_vkGetSemaphoreCounterValue
#define vkWaitSemaphores pfn_vkWaitSemaphores
#endif

namespace irox {

void GPUMemoryManager::init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
	m_instance = instance;
	m_physicalDevice = physicalDevice;
	m_device = device;

	VmaAllocatorCreateInfo allocInfo = {
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = physicalDevice,
		.device = device,
		.instance = instance,
		.vulkanApiVersion = VK_API_VERSION_1_3,
	};

#if defined(IROX_PLATFORM_ANDROID)
	// Vulkan 1.2
	pfn_vkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress) vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddress");
	pfn_vkGetSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValue) vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValue");
	pfn_vkWaitSemaphores = (PFN_vkWaitSemaphores) vkGetDeviceProcAddr(device, "vkWaitSemaphores");

	// KHR-fallback
	if (!pfn_vkGetBufferDeviceAddress) {
		pfn_vkGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress) vkGetDeviceProcAddr(device, "vkGetBufferDeviceAddressKHR");
	}
	if (!pfn_vkGetSemaphoreCounterValue) {
		pfn_vkGetSemaphoreCounterValue = (PFN_vkGetSemaphoreCounterValue) vkGetDeviceProcAddr(device, "vkGetSemaphoreCounterValueKHR");
	}
	if (!pfn_vkWaitSemaphores) {
		pfn_vkWaitSemaphores = (PFN_vkWaitSemaphores) vkGetDeviceProcAddr(device, "vkWaitSemaphoresKHR");
	}

	if (!pfn_vkGetSemaphoreCounterValue) {
		std::cerr << "FATAL: Timeline Semaphores not supported on this device!\n";
		std::exit(-1);
	}

	VmaVulkanFunctions vulkanFunctions = {};
	vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
	vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
	allocInfo.pVulkanFunctions = &vulkanFunctions;
#endif

	VK_CHECK(vmaCreateAllocator(&allocInfo, &m_allocator));

	// Staging buffer is initialized by default
	m_staging.memory = createHostMappedBuffer(m_staging.totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

	VmaVirtualBlockCreateInfo blockInfo = {
		.size = m_staging.totalSize,
		.flags = VMA_VIRTUAL_BLOCK_CREATE_LINEAR_ALGORITHM_BIT,
	};

	VK_CHECK(vmaCreateVirtualBlock(&blockInfo, &m_staging.virtualBlock));
}

void GPUMemoryManager::shutdown() {
	if (m_allocator == VK_NULL_HANDLE) {
		return;
	}
	// Destroy Staging Buffer
	std::lock_guard<std::mutex> lockStaging(m_stagingMutex);
	if (m_staging.virtualBlock != VK_NULL_HANDLE) {
		vmaClearVirtualBlock(m_staging.virtualBlock);
		vmaDestroyVirtualBlock(m_staging.virtualBlock);
		m_staging.virtualBlock = VK_NULL_HANDLE;
	}

	destroyHostMappedBuffer(m_staging.memory);
	m_staging.inFlightAllocations.clear();

	// Destroy Megabuffer if it exists
	std::lock_guard<std::mutex> lockMega(m_megaBufferMutex);
	for (auto& chunk : m_megaBufferChunks) {
		if (chunk.virtualBlock != VK_NULL_HANDLE) {
			vmaClearVirtualBlock(chunk.virtualBlock);
			vmaDestroyVirtualBlock(chunk.virtualBlock);
			chunk.virtualBlock = VK_NULL_HANDLE;
		}
		if (chunk.allocated != VK_NULL_HANDLE && chunk.allocation != VK_NULL_HANDLE) {
			vmaDestroyBuffer(m_allocator, chunk.allocated, chunk.allocation);
			chunk.allocated = VK_NULL_HANDLE;
			chunk.allocation = VK_NULL_HANDLE;
		}
	}
	m_megaBufferChunks.clear();


	// Destroy and reset everything else
	vmaDestroyAllocator(m_allocator);
	m_allocator = VK_NULL_HANDLE;

	m_device = VK_NULL_HANDLE;
	m_physicalDevice = VK_NULL_HANDLE;
	m_instance = VK_NULL_HANDLE;
}


// Buffer management
AllocatedData GPUMemoryManager::createBuffer(
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VmaMemoryUsage memoryUsage,
	VmaAllocationCreateFlags flags
) {
	AllocatedData outBuffer {};

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
	};

	VmaAllocationCreateInfo allocInfo = {
		.flags = flags,
		.usage = memoryUsage,
	};

	VK_CHECK(vmaCreateBuffer(
		m_allocator,
		&bufferInfo,
		&allocInfo,
		&outBuffer.buffer,
		&outBuffer.allocation,
		nullptr
	));

	// Fetch Device Address if requested
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		VkBufferDeviceAddressInfo bdaInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = outBuffer.buffer,
		};
		outBuffer.deviceAddress = vkGetBufferDeviceAddress(m_device, &bdaInfo);
	}

	return outBuffer;
}

void GPUMemoryManager::destroyBuffer(AllocatedData& allocated) {
	if (allocated.buffer != VK_NULL_HANDLE && allocated.allocation != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_allocator, allocated.buffer, allocated.allocation);
		allocated.buffer = VK_NULL_HANDLE;
		allocated.allocation = VK_NULL_HANDLE;
		allocated.deviceAddress = 0;
	}
}

// Image Management
AllocatedImage GPUMemoryManager::createImage(
	const VkImageCreateInfo& imageInfo,
	VmaMemoryUsage memoryUsage,
	VkImageAspectFlags aspectFlags
) {
	AllocatedImage image {};

	VmaAllocationCreateInfo createInfo {
		.usage = memoryUsage,
	};

	VK_CHECK(vmaCreateImage(m_allocator, &imageInfo, &createInfo, &image.image, &image.allocation, nullptr));

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	if (imageInfo.imageType == VK_IMAGE_TYPE_1D) {
		viewType = (imageInfo.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;
	} else if (imageInfo.imageType & VK_IMAGE_TYPE_3D) {
		viewType = VK_IMAGE_VIEW_TYPE_3D;
	} else if (imageInfo.imageType & VK_IMAGE_TYPE_2D) {
		if (imageInfo.flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) {
			viewType = (imageInfo.arrayLayers > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
		} else if (imageInfo.arrayLayers > 1) {
			viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		} else {
			viewType = VK_IMAGE_VIEW_TYPE_2D;
		}
	}

	VkImageViewCreateInfo viewCreateInfo {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.image,
		.viewType = viewType,
		.format = imageInfo.format,
	};
	viewCreateInfo.subresourceRange.aspectMask = aspectFlags;
	viewCreateInfo.subresourceRange.baseMipLevel = 0;
	viewCreateInfo.subresourceRange.levelCount = imageInfo.mipLevels;
	viewCreateInfo.subresourceRange.baseArrayLayer = 0;
	viewCreateInfo.subresourceRange.layerCount = imageInfo.arrayLayers;

	VK_CHECK(vkCreateImageView(m_device, &viewCreateInfo, nullptr, &image.defaultView));

	return image;
}

void GPUMemoryManager::destroyImage(AllocatedImage& image) {
	if (image.defaultView != VK_NULL_HANDLE) {
		vkDestroyImageView(m_device, image.defaultView, nullptr);
		image.defaultView = VK_NULL_HANDLE;
	}

	if (image.image != VK_NULL_HANDLE && image.allocation != VK_NULL_HANDLE) {
		vmaDestroyImage(m_allocator, image.image, image.allocation);
		image.image = VK_NULL_HANDLE;
		image.allocation = VK_NULL_HANDLE;
	}
}

// MegaBuffer (vertices, indeces and other stuff)

uint32_t GPUMemoryManager::createMegaBufferChunk(VkDeviceSize size) {
	MegaBufferChunk chunk;
	chunk.capacity = size;

	VkBufferCreateInfo bufferInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
				 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
				 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
				 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};

	VmaAllocationCreateInfo allocInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo,
			 &chunk.allocated, &chunk.allocation, nullptr));

	VkBufferDeviceAddressInfo bdaInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = chunk.allocated,
	};
	chunk.deviceAddress = vkGetBufferDeviceAddress(m_device, &bdaInfo);

	VmaVirtualBlockCreateInfo blockInfo = {
		.size = size,
		.flags = 0, // Default TLSF mode
	};
	VK_CHECK(vmaCreateVirtualBlock(&blockInfo, &chunk.virtualBlock));

	m_megaBufferChunks.push_back(chunk);
	return static_cast<uint32_t>(m_megaBufferChunks.size() - 1);
}

MeshAllocation GPUMemoryManager::allocateGeometry(VkDeviceSize sizeInBytes, VkDeviceSize alignment) {
	std::lock_guard<std::mutex> lock(m_megaBufferMutex);

	VmaVirtualAllocationCreateInfo allocRequest = {
		.size = sizeInBytes,
		.alignment = alignment,
	};

	VmaVirtualAllocation virtualAlloc = VK_NULL_HANDLE;
	VkDeviceSize offset = 0;
	int targetChunk = -1;

	for (size_t i = 0; i < m_megaBufferChunks.size(); ++i) {
		VkResult res = vmaVirtualAllocate(m_megaBufferChunks[i].virtualBlock,
										  &allocRequest, &virtualAlloc, &offset);
		if (res == VK_SUCCESS) {
			targetChunk = static_cast<int>(i);
			break;
		}
	}

	if (targetChunk == -1) {
		VkDeviceSize newChunkSize = std::max(m_defaultChunkSize, sizeInBytes * 2);
		targetChunk = static_cast<int>(createMegaBufferChunk(newChunkSize));

		VK_CHECK(vmaVirtualAllocate(m_megaBufferChunks[targetChunk].virtualBlock,
				 &allocRequest, &virtualAlloc, &offset));
	}

	const auto& chunk = m_megaBufferChunks[targetChunk];

	MeshAllocation result {
		.chunkIndex = static_cast<uint32_t>(targetChunk),
		.allocated = chunk.allocated,
		.offset = offset,
		.deviceAddress = chunk.deviceAddress + offset,
		.virtualAlloc = virtualAlloc,
		.size = sizeInBytes,
	};

	return result;
}

void GPUMemoryManager::freeGeometry(const MeshAllocation& alloc) {
	std::lock_guard<std::mutex> lock(m_megaBufferMutex);

	if (alloc.chunkIndex < m_megaBufferChunks.size()) {
		auto& chunk = m_megaBufferChunks[alloc.chunkIndex];
		vmaVirtualFree(chunk.virtualBlock, alloc.virtualAlloc);
	}
}

// Staging Ring Buffer

void GPUMemoryManager::retireCompletedStagingSlices(VkSemaphore timelineSemaphore) {
	// Note: Caller must hold m_stagingMutex
	uint64_t completedValue = 0;
	vkGetSemaphoreCounterValue(m_device, timelineSemaphore, &completedValue);

	while (!m_staging.inFlightAllocations.empty()) {
		const auto& oldest = m_staging.inFlightAllocations.front();

		if (completedValue >= oldest.timelineWaitValue) {
			vmaVirtualFree(m_staging.virtualBlock, oldest.virtualAlloc);
			m_staging.inFlightAllocations.pop_front();
		} else {
			break;
		}
	}
}

void GPUMemoryManager::sendStagingData(
	VkCommandBuffer transferCmd,
	VkSemaphore timelineSemaphore,
	uint64_t currentTimelineSubmissionValue,
	VkBuffer dstBuffer,
	VkDeviceSize dstOffset,
	const void* srcData,
	VkDeviceSize sizeInBytes,
	VkDeviceSize alignment
) {
	std::lock_guard<std::mutex> lock(m_stagingMutex);

	retireCompletedStagingSlices(timelineSemaphore); // Clears data that was succesfully ported to megabuffer

	VmaVirtualAllocationCreateInfo allocRequest = {
		.size = sizeInBytes,
		.alignment = alignment,
	};

	VmaVirtualAllocation virtualAlloc = VK_NULL_HANDLE;
	VkDeviceSize stagingOffset = 0;

	VkResult res = vmaVirtualAllocate(m_staging.virtualBlock,
									  &allocRequest,
									  &virtualAlloc,
									  &stagingOffset);

	while (res != VK_SUCCESS) {
		if (m_staging.inFlightAllocations.empty()) {
			throw std::runtime_error("GPUMemoryManager: Single asset exceeds total staging capacity (" +
									 std::to_string(m_staging.totalSize / (1024 * 1024)) + " MiB).");
		}

		uint64_t nextValueToWait = m_staging.inFlightAllocations.front().timelineWaitValue;

		VkSemaphoreWaitInfo waitInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &timelineSemaphore,
			.pValues = &nextValueToWait,
		};

		vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);

		retireCompletedStagingSlices(timelineSemaphore);

		res = vmaVirtualAllocate(m_staging.virtualBlock,
								 &allocRequest,
								 &virtualAlloc,
								 &stagingOffset);
	}

	std::memcpy(static_cast<uint8_t*>(m_staging.memory.mappedData) + stagingOffset, srcData, sizeInBytes);
	vmaFlushAllocation(m_allocator, m_staging.memory.allocated.allocation, stagingOffset, sizeInBytes);

	VkBufferCopy copyRegion = {
		.srcOffset = stagingOffset,
		.dstOffset = dstOffset,
		.size = sizeInBytes,
	};

	vkCmdCopyBuffer(transferCmd, m_staging.memory.allocated.buffer, dstBuffer, 1, &copyRegion);
	m_staging.inFlightAllocations.push_back({ virtualAlloc, currentTimelineSubmissionValue });
}

HostMappedBuffer GPUMemoryManager::createHostMappedBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
	HostMappedBuffer buffer {};
	buffer.size = size;

	VkBufferCreateInfo bufferInfo {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
	};

	VmaAllocationCreateInfo allocInfo {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
				 VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo resultInfo;
	VK_CHECK(vmaCreateBuffer(
		m_allocator,
		&bufferInfo,
		&allocInfo,
		&buffer.allocated.buffer,
		&buffer.allocated.allocation,
		&resultInfo
	));

	buffer.mappedData = resultInfo.pMappedData;

	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		VkBufferDeviceAddressInfo bdaInfo = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = buffer.allocated.buffer,
		};
		buffer.allocated.deviceAddress = vkGetBufferDeviceAddress(m_device, &bdaInfo);
	}

	return buffer;
}

void GPUMemoryManager::destroyHostMappedBuffer(HostMappedBuffer& allocated) {
	destroyBuffer(allocated.allocated);
	allocated.mappedData = nullptr;
	allocated.size = 0;
}

void GPUMemoryManager::sendStagingImageData(
	VkCommandBuffer transferCmd,
	VkSemaphore timelineSemaphore,
	uint64_t currentTimelineSubmissionValue,
	VkImage dstImage,
	VkExtent3D extent,
	const void* srcData,
	VkDeviceSize sizeInBytes,
	VkImageLayout currentLayout,
	VkImageLayout finalLayout,
	VkImageAspectFlags aspectMask
) {
	std::lock_guard<std::mutex> lock(m_stagingMutex);
	retireCompletedStagingSlices(timelineSemaphore);

	// 16 bytes is a Vulkan texel alignment requirement
	const VkDeviceSize alignment = 16;
	VmaVirtualAllocationCreateInfo allocRequest = {
		.size = sizeInBytes,
		.alignment = alignment,
	};

	VmaVirtualAllocation virtualAlloc = VK_NULL_HANDLE;
	VkDeviceSize stagingOffset = 0;

	VkResult res = vmaVirtualAllocate(
		m_staging.virtualBlock,
		&allocRequest,
		&virtualAlloc,
		&stagingOffset
	);

	while (res != VK_SUCCESS) {
		if (m_staging.inFlightAllocations.empty()) {
			throw std::runtime_error("GPUMemoryManager: Texture size exceeds total staging capacity (" +
									 std::to_string(m_staging.totalSize / (1024 * 1024)) + " MiB).");
		}

		uint64_t nextValueToWait = m_staging.inFlightAllocations.front().timelineWaitValue;

		VkSemaphoreWaitInfo waitInfo = {
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
			.semaphoreCount = 1,
			.pSemaphores = &timelineSemaphore,
			.pValues = &nextValueToWait,
		};

		vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX);

		retireCompletedStagingSlices(timelineSemaphore);

		res = vmaVirtualAllocate(
			m_staging.virtualBlock,
			&allocRequest,
			&virtualAlloc,
			&stagingOffset
		);
	}

	uint8_t* targetPtr = static_cast<uint8_t*>(m_staging.memory.mappedData);
	std::memcpy(targetPtr + stagingOffset, srcData, sizeInBytes);

	vmaFlushAllocation(m_allocator, m_staging.memory.allocated.allocation, stagingOffset, sizeInBytes);

	VkImageMemoryBarrier barrierToTransfer = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = currentLayout,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = dstImage,
		.subresourceRange = {
			.aspectMask = aspectMask,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = extent.depth > 1 ? 1 : extent.depth,
		}
	};

	vkCmdPipelineBarrier(
		transferCmd,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrierToTransfer
	);

	VkBufferImageCopy region = {
		.bufferOffset = stagingOffset,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = aspectMask,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset = { 0, 0, 0 },
		.imageExtent = extent,
	};

	vkCmdCopyBufferToImage(
		transferCmd,
		m_staging.memory.allocated.buffer,
		dstImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	VkImageMemoryBarrier barrierToShader = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = finalLayout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = dstImage,
		.subresourceRange = {
			.aspectMask = aspectMask,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	};

	vkCmdPipelineBarrier(
		transferCmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrierToShader
	);

	m_staging.inFlightAllocations.push_back({ virtualAlloc, currentTimelineSubmissionValue });
}


} // namespace irox