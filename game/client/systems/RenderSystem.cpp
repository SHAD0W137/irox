#include "game/client/systems/RenderSystem.h"
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
    if (!world.isAlive(cameraEntity) || !world.hasComponent<CameraComponent>(cameraEntity)) return;

    const auto& cam = world.getComponent<CameraComponent>(cameraEntity);
    const glm::mat4 viewProj = cam.projMatrix * cam.viewMatrix;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    auto view = world.View<MeshComponent, TransformComponent>();

    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
    VkDeviceSize currentVertexOffset = VK_WHOLE_SIZE;

    VkBuffer currentIndexBuffer = VK_NULL_HANDLE;
    VkDeviceSize currentIndexOffset = VK_WHOLE_SIZE;

    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;

    for (ecs::Entity e : view) {
        const auto& mesh = view.get<MeshComponent>(e);
        const auto& transform = view.get<TransformComponent>(e);

        if (mesh.indexCount == 0 || mesh.defaultDescriptorSet == VK_NULL_HANDLE) {
            continue;
        }

        // Push Constants: Model-View-Projection Matrix
        PushConstants pc {
            .mvp = viewProj * transform.getMatrix()
        };
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        // Bind vertex buffer: Check BOTH buffer handle and offset
        if (mesh.vertexAlloc.allocated != currentVertexBuffer || mesh.vertexAlloc.offset != currentVertexOffset) {
            currentVertexBuffer = mesh.vertexAlloc.allocated;
            currentVertexOffset = mesh.vertexAlloc.offset;

            VkBuffer vBuffers[] = { currentVertexBuffer };
            VkDeviceSize offsets[] = { currentVertexOffset };
            vkCmdBindVertexBuffers(cmd, 0, 1, vBuffers, offsets);
        }

        // Bind index buffer: Check BOTH buffer handle and offset
        if (mesh.indexAlloc.allocated != currentIndexBuffer || mesh.indexAlloc.offset != currentIndexOffset) {
            currentIndexBuffer = mesh.indexAlloc.allocated;
            currentIndexOffset = mesh.indexAlloc.offset;

            vkCmdBindIndexBuffer(cmd, currentIndexBuffer, currentIndexOffset, VK_INDEX_TYPE_UINT32);
        }

        // Bind descriptor set
        if (mesh.defaultDescriptorSet != currentDescriptorSet) {
            currentDescriptorSet = mesh.defaultDescriptorSet;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &currentDescriptorSet, 0, nullptr);
        }

        // Draw sub-allocated primitive
        vkCmdDrawIndexed(cmd, mesh.indexCount, 1, mesh.firstIndex, mesh.vertexOffset, 0);
    }
}

} // namespace irox