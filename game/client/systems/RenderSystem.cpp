#include "RenderSystem.h"
#include "game/shared/components/TransformComponent.h"
#include "game/client/components/MeshComponent.h"
#include "game/client/components/CameraComponent.h"
#include "engine/render/RenderTypes.h"

namespace irox {

void RenderSystem::recordCommands(
    ecs::World& world,
    VkCommandBuffer cmd,
    VkPipelineLayout layout,
    VkPipeline pipeline,
    ecs::Entity cameraEntity
) {
    if (pipeline == VK_NULL_HANDLE || layout == VK_NULL_HANDLE) return;
    if (!world.hasComponent<CameraComponent>(cameraEntity)) return;

    auto& cam = world.getComponent<CameraComponent>(cameraEntity);
    auto& meshPool = world.getPool<MeshComponent>();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    for (ecs::Entity e : meshPool.entities()) {
        if (!world.hasComponent<TransformComponent>(e)) continue;

        auto& transform = world.getComponent<TransformComponent>(e);
        auto& mesh = meshPool.get(e);

        PushConstants pc {
            .mvp = cam.projMatrix * cam.viewMatrix * transform.getMatrix()
        };
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vBuffers[] = { mesh.vertexAlloc.allocated };
        VkDeviceSize offsets[] = { mesh.vertexAlloc.offset };
        vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, mesh.indexAlloc.allocated, mesh.indexAlloc.offset, VK_INDEX_TYPE_UINT32);

        for (const auto& primitive : mesh.primitives) {
            VkDescriptorSet ds = mesh.defaultDescriptorSet;
            if (primitive.materialIndex >= 0 && primitive.materialIndex < static_cast<int32_t>(mesh.materialDescriptorSets.size())) {
                ds = mesh.materialDescriptorSets[primitive.materialIndex];
            }

            if (ds == VK_NULL_HANDLE) {
                continue;
            }

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &ds, 0, nullptr);
            vkCmdDrawIndexed(cmd, primitive.indexCount, 1, primitive.firstIndex, primitive.vertexOffset, 0);
        }
    }
}

} // namespace irox