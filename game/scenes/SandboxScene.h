#pragma once
#include "engine/ecs/ECS.h"
#include "engine/render/VulkanContext.h"
#include "engine/network/NetworkPackets.h"
#include "game/client/components/MeshComponent.h"
#include <unordered_map>

namespace irox {

enum class NetworkMode : uint8_t {
    Client,        // Client with prediction & server authority
    Singleplayer,  // Local simulation only (zero networking, zero prediction overhead)
    ListenServer   // Host player (simulates world, local player has no prediction, acts as server)
};

class SandboxScene {
public:
    void init(ecs::World& world, VulkanContext& context, NetworkMode mode);
    void shutdown(ecs::World& world, VulkanContext& context);

    void fixedUpdate(ecs::World& world, float fixedDt);
    void update(ecs::World& world, float aspectRatio, float deltaTime);
    void render(ecs::World& world, VkCommandBuffer cmd, VulkanContext& context);

    void setLocalNetworkId(uint32_t netId) { m_localNetworkId = netId; }
    void applyWorldSnapshot(ecs::World& world, VulkanContext& context, const net::WorldSnapshotPacket& snapshot);

    [[nodiscard]] ecs::Entity getLocalPlayerEntity() const noexcept { return m_localPlayerEntity; }
    [[nodiscard]] NetworkMode getNetworkMode() const noexcept { return m_networkMode; }

    [[nodiscard]] uint32_t getLocalNetworkId() const noexcept { return m_localNetworkId; }

private:
    NetworkMode m_networkMode = NetworkMode::Client;
    ecs::Entity m_cameraEntity = ecs::NULL_ENTITY;
    ecs::Entity m_planeEntity = ecs::NULL_ENTITY;
    ecs::Entity m_localPlayerEntity = ecs::NULL_ENTITY;

    uint32_t m_localNetworkId = 0;
    std::unordered_map<uint32_t, ecs::Entity> m_networkEntities;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

    irox::AllocatedImage m_gridTexture;
    VkDescriptorSet m_gridDescriptorSet = VK_NULL_HANDLE;

    irox::AllocatedImage m_localPlayerTexture;
    VkDescriptorSet m_localPlayerDescriptorSet = VK_NULL_HANDLE;

    irox::AllocatedImage m_remotePlayerTexture;
    VkDescriptorSet m_remotePlayerDescriptorSet = VK_NULL_HANDLE;

    MeshComponent m_sharedCapsuleMesh;

    void createPipeline(VulkanContext& context);
    void createGridTexture(VulkanContext& context);
    void createPlayerTextures(VulkanContext& context);
};

} // namespace irox