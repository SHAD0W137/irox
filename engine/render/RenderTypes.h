#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace irox {

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;

	static VkVertexInputBindingDescription getBindingDescription() {
		return {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
		};
	}

	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
		return {
			{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
			{.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
			{.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,    .offset = offsetof(Vertex, uv) }
		};
	}
};

struct PushConstants {
	glm::mat4 mvp;
};

struct Primitive {
	uint32_t firstIndex = 0;
	uint32_t indexCount = 0;
	int32_t vertexOffset = 0;
	int32_t materialIndex = -1;
};

} // namespace irox