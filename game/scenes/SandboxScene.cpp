#include "game/scenes/SandboxScene.h"
#include "engine/render/MeshGenerator.h"
#include "engine/render/ShaderCompiler.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/PhysicsFactory.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/CapsuleColliderComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include "game/shared/components/PlayerInputComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/PredictionComponent.h"
#include "game/client/components/CameraComponent.h"
#include "game/client/systems/CameraSystem.h"
#include "game/client/systems/RenderSystem.h"
#include "game/shared/systems/CharacterMovementSystem.h"
#include "game/shared/systems/PhysicsSystem.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <cmath>

constexpr bool ENABLE_PREDICTION = true;


namespace irox {

void SandboxScene::init(ecs::World& world, VulkanContext& context) {
    createPipeline(context);
    createGridTexture(context);
    createPlayerTextures(context);

    // Initializeing Box3D world
    auto& physics = world.setSingleton<physics::PhysicsWorldSingleton>();
    physics.init(-18.0f);

    // Creating physical floor below Y=0 surface
    auto groundRb = physics::PhysicsFactory::createStaticGroundBox(
        physics.worldId, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(25.0f, 0.5f, 25.0f)
    );

    auto& mem = context.getMemoryManager();

    // Camera
    m_cameraEntity = world.createEntity();
    world.addComponent<CameraComponent>(m_cameraEntity, CameraComponent {
        .distance = 7.0f,
        .target = { 0.0f, 1.0f, 0.0f }
    });

    // Visual floor surface
    auto planeData = MeshGenerator::createPlane(50.0f, 50.0f, 25.0f, 25.0f);
    m_planeEntity = world.createEntity();
    world.addComponent<TransformComponent>(m_planeEntity, TransformComponent { .position = { 0.0f, 0.0f, 0.0f } });
    // world.addComponent<RigidBodyComponent>(m_planeEntity, groundRb);

    MeshComponent planeMesh;
    VkDeviceSize pVSize = sizeof(Vertex) * planeData.vertices.size();
    VkDeviceSize pISize = sizeof(uint32_t) * planeData.indices.size();
    planeMesh.vertexAlloc = mem.allocateGeometry(pVSize);
    planeMesh.indexAlloc = mem.allocateGeometry(pISize);
    planeMesh.primitives = { planeData.primitive };
    planeMesh.defaultDescriptorSet = m_gridDescriptorSet;

    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    mem.sendStagingData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        planeMesh.vertexAlloc.allocated, planeMesh.vertexAlloc.offset, planeData.vertices.data(), pVSize);
    mem.sendStagingData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        planeMesh.indexAlloc.allocated, planeMesh.indexAlloc.offset, planeData.indices.data(), pISize);
    context.endSingleTimeCommands(cmd);

    world.addComponent<MeshComponent>(m_planeEntity, planeMesh);

    // Capsule mesh
    CapsuleColliderComponent capsuleCollider { .radius = 0.4f, .halfHeight = 0.5f };
    auto capsuleData = MeshGenerator::createCapsule(capsuleCollider.radius, capsuleCollider.halfHeight);

    VkDeviceSize cVSize = sizeof(Vertex) * capsuleData.vertices.size();
    VkDeviceSize cISize = sizeof(uint32_t) * capsuleData.indices.size();
    m_sharedCapsuleMesh.vertexAlloc = mem.allocateGeometry(cVSize);
    m_sharedCapsuleMesh.indexAlloc = mem.allocateGeometry(cISize);
    m_sharedCapsuleMesh.primitives = { capsuleData.primitive };
    m_sharedCapsuleMesh.defaultDescriptorSet = m_localPlayerDescriptorSet;

    cmd = context.beginSingleTimeCommands();
    mem.sendStagingData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        m_sharedCapsuleMesh.vertexAlloc.allocated, m_sharedCapsuleMesh.vertexAlloc.offset, capsuleData.vertices.data(), cVSize);
    mem.sendStagingData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        m_sharedCapsuleMesh.indexAlloc.allocated, m_sharedCapsuleMesh.indexAlloc.offset, capsuleData.indices.data(), cISize);
    context.endSingleTimeCommands(cmd);

    m_localPlayerEntity = world.createEntity();
    world.addComponent<TransformComponent>(m_localPlayerEntity, TransformComponent { .position = { 0.0f, 3.0f, 0.0f } });
    world.addComponent<CapsuleColliderComponent>(m_localPlayerEntity, capsuleCollider);
    world.addComponent<CharacterControllerComponent>(m_localPlayerEntity, CharacterControllerComponent {});
    world.addComponent<PlayerInputComponent>(m_localPlayerEntity, PlayerInputComponent {});
    world.addComponent<MeshComponent>(m_localPlayerEntity, m_sharedCapsuleMesh);

    auto playerRb = physics::PhysicsFactory::createCharacterCapsule(
        physics.worldId, glm::vec3(0.0f, 3.0f, 0.0f), 0.4f, 0.5f
    );
    world.addComponent<RigidBodyComponent>(m_localPlayerEntity, playerRb);

    // Prediction buffer for local player
    LocalPlayerPredictionComponent predComp {};
    predComp.stateHistory[0] = PredictedState {
        .tick = 0,
        .position = { 0.0f, 3.0f, 0.0f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .isGrounded = false
    };
    world.addComponent<LocalPlayerPredictionComponent>(m_localPlayerEntity, predComp);
}

void SandboxScene::createGridTexture(VulkanContext& context) {
    const uint32_t S = 64;
    std::vector<uint32_t> pixels(S * S);
    for (uint32_t y = 0; y < S; ++y) {
        for (uint32_t x = 0; x < S; ++x) {
            bool isBorder = (x == 0 || y == 0 || x == S - 1 || y == S - 1);
            pixels[y * S + x] = isBorder ? 0xFFFFFFFF : 0xFF2A2A2A;
        }
    }

    VkImageCreateInfo imgInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { S, S, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    auto& mem = context.getMemoryManager();
    m_gridTexture = mem.createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    mem.sendStagingImageData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        m_gridTexture.image, { S, S, 1 }, pixels.data(), pixels.size() * sizeof(uint32_t));
    context.endSingleTimeCommands(cmd);

    m_gridDescriptorSet = context.createTextureDescriptor(m_gridTexture.defaultView);
}

void SandboxScene::createPlayerTextures(VulkanContext& context) {
    auto& mem = context.getMemoryManager();
    VkImageCreateInfo imgInfo {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { 16, 16, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    std::vector<uint32_t> localPixels(16 * 16, 0xFF00E5FF);
    m_localPlayerTexture = mem.createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    VkCommandBuffer cmd = context.beginSingleTimeCommands();
    mem.sendStagingImageData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        m_localPlayerTexture.image, { 16, 16, 1 }, localPixels.data(), localPixels.size() * sizeof(uint32_t));
    context.endSingleTimeCommands(cmd);
    m_localPlayerDescriptorSet = context.createTextureDescriptor(m_localPlayerTexture.defaultView);

    std::vector<uint32_t> remotePixels(16 * 16, 0xFFFF6D00);
    m_remotePlayerTexture = mem.createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    cmd = context.beginSingleTimeCommands();
    mem.sendStagingImageData(cmd, context.getUploadTimelineSemaphore(), ++context.getUploadTimelineValue(),
        m_remotePlayerTexture.image, { 16, 16, 1 }, remotePixels.data(), remotePixels.size() * sizeof(uint32_t));
    context.endSingleTimeCommands(cmd);
    m_remotePlayerDescriptorSet = context.createTextureDescriptor(m_remotePlayerTexture.defaultView);
}

void SandboxScene::createPipeline(VulkanContext& context) {
    VkDevice device = context.getDevice();

    VkPushConstantRange pcr { .stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = sizeof(PushConstants) };
    VkDescriptorSetLayout setLayout = context.getDescriptorSetLayout();

    VkPipelineLayoutCreateInfo plInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr
    };
    vkCreatePipelineLayout(device, &plInfo, nullptr, &m_pipelineLayout);

    ShaderCompiler compiler;
    VkShaderModule vertShader = compiler.compileShader(device, "cube", "vertexMain", SLANG_STAGE_VERTEX);
    VkShaderModule fragShader = compiler.compileShader(device, "cube", "fragmentMain", SLANG_STAGE_FRAGMENT);

    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertShader, .pName = "main" },
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragShader, .pName = "main" }
    };

    auto bDesc = Vertex::getBindingDescription();
    auto aDesc = Vertex::getAttributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vInput {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bDesc,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(aDesc.size()),
        .pVertexAttributeDescriptions = aDesc.data()
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo vpState {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1
    };

    VkPipelineRasterizationStateCreateInfo raster {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo ms {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineDepthStencilStateCreateInfo ds {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS
    };

    VkPipelineColorBlendAttachmentState cba {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo cb {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba
    };

    std::vector<VkDynamicState> dyns = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dyns.size()), .pDynamicStates = dyns.data()
    };

    VkGraphicsPipelineCreateInfo pipeInfo {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vInput, .pInputAssemblyState = &inputAssembly,
        .pViewportState = &vpState, .pRasterizationState = &raster,
        .pMultisampleState = &ms, .pDepthStencilState = &ds,
        .pColorBlendState = &cb, .pDynamicState = &dyn,
        .layout = m_pipelineLayout, .renderPass = context.getRenderPass()
    };

    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_graphicsPipeline));

    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
}

void SandboxScene::fixedUpdate(ecs::World& world, float fixedDt) {
    CharacterMovementSystem::update(world, fixedDt);
    PhysicsSystem::update(world, fixedDt);
}

void SandboxScene::applyWorldSnapshot(ecs::World& world, VulkanContext& context, const net::WorldSnapshotPacket& snapshot) {
    for (uint32_t i = 0; i < snapshot.entityCount; ++i) {
        const auto& data = snapshot.entities[i];
        glm::vec3 pos(data.posX, data.posY, data.posZ);
        glm::vec3 vel(data.velX, data.velY, data.velZ);

        if (!m_networkEntities.contains(data.networkId)) {
            ecs::Entity entityToUse = ecs::NULL_ENTITY;

            if (data.networkId == m_localNetworkId) {
                entityToUse = m_localPlayerEntity;
            } else {
                entityToUse = world.createEntity();
                world.addComponent<TransformComponent>(entityToUse, TransformComponent { .position = pos });
                world.addComponent<CapsuleColliderComponent>(entityToUse, CapsuleColliderComponent { .radius = 0.4f, .halfHeight = 0.5f });

                MeshComponent remoteMesh = m_sharedCapsuleMesh;
                remoteMesh.defaultDescriptorSet = m_remotePlayerDescriptorSet;
                world.addComponent<MeshComponent>(entityToUse, remoteMesh);

                SDL_Log("[Client] Spawned Remote Player (NetID=%u)", data.networkId);
            }

            m_networkEntities[data.networkId] = entityToUse;
        }


        // Local Player: Error checking and rollback resimulation
        if (ENABLE_PREDICTION && data.networkId == m_localNetworkId && world.hasComponent<LocalPlayerPredictionComponent>(m_localPlayerEntity)) {
            auto& pred = world.getComponent<LocalPlayerPredictionComponent>(m_localPlayerEntity);
            auto& rb = world.getComponent<RigidBodyComponent>(m_localPlayerEntity);
            auto& controller = world.getComponent<CharacterControllerComponent>(m_localPlayerEntity);
            auto& tf = world.getComponent<TransformComponent>(m_localPlayerEntity);

            const uint32_t ackTick = snapshot.acknowledgedClientTick;

            // Validate only fresh confirmations within current tick
            if (ackTick > pred.lastAcknowledgedServerTick && ackTick <= pred.currentClientTick) {
                pred.lastAcknowledgedServerTick = ackTick;

                const size_t ackSlot = ackTick % LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                const auto& predictedState = pred.stateHistory[ackSlot];

                // Threshold squared / 5mm = 0.005m
                glm::vec3 delta = pos - predictedState.position;
                constexpr float THRESHOLD_SQ = 0.005f * 0.005f;

                if (glm::dot(delta, delta) > THRESHOLD_SQ) {
                    // Keep the error vector for rendering smoothing
                    pred.visualErrorOffset += (predictedState.position - pos);

                    // Rollback RB according to server authority
                    b3Body_SetTransform(rb.bodyId, b3Vec3 { pos.x, pos.y, pos.z }, b3Body_GetRotation(rb.bodyId));
                    b3Body_SetLinearVelocity(rb.bodyId, b3Vec3 { vel.x, vel.y, vel.z });
                    controller.isGrounded = (data.isGrounded != 0);

                    auto& physics = world.getSingleton<physics::PhysicsWorldSingleton>();
                    constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;

                    // Resimulating unacknowledged ticks: (ackTick + 1) -> currentClientTick
                    for (uint32_t t = ackTick + 1; t <= pred.currentClientTick; ++t) {
                        const size_t slot = t % LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                        const auto& input = pred.inputHistory[slot];

                        // Applying input history
                        CharacterMovementSystem::step(rb, controller, input.moveDir, input.jumpRequested);

                        // Physical world step
                        b3World_Step(physics.worldId, FIXED_TIMESTEP, 4);

                        // Rewrite predicted state
                        b3Vec3 newPos = b3Body_GetPosition(rb.bodyId);
                        b3Vec3 newVel = b3Body_GetLinearVelocity(rb.bodyId);

                        pred.stateHistory[slot] = PredictedState {
                            .tick = t,
                            .position = { newPos.x, newPos.y, newPos.z },
                            .velocity = { newVel.x, newVel.y, newVel.z },
                            .isGrounded = controller.isGrounded
                        };
                    }

                    // Sync final position in TransformComponent
                    const size_t currSlot = pred.currentClientTick % LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                    tf.position = pred.stateHistory[currSlot].position;
                }
            }
        }
        // Deleted player: Direct position update
        else {
            ecs::Entity entity = m_networkEntities[data.networkId];
            if (world.hasComponent<TransformComponent>(entity)) {
                auto& tf = world.getComponent<TransformComponent>(entity);
                tf.position = pos;

                // If prediction disabled, physicall collider is teleported
                if (!ENABLE_PREDICTION && entity == m_localPlayerEntity) {
                    auto& rb = world.getComponent<RigidBodyComponent>(entity);
                    b3Body_SetTransform(rb.bodyId, b3Vec3 { pos.x, pos.y, pos.z }, b3Body_GetRotation(rb.bodyId));
                    b3Body_SetLinearVelocity(rb.bodyId, b3Vec3 { vel.x, vel.y, vel.z });
                }
            }
        }
    }

    // Delete disconnected players
    for (auto it = m_networkEntities.begin(); it != m_networkEntities.end(); ) {
        uint32_t netId = it->first;
        bool exists = false;
        for (uint32_t i = 0; i < snapshot.entityCount; ++i) {
            if (snapshot.entities[i].networkId == netId) { exists = true; break; }
        }
        if (!exists) {
            if (it->second != m_localPlayerEntity) {
                world.destroyEntity(it->second);
            }
            it = m_networkEntities.erase(it);
        } else {
            ++it;
        }
    }
}

void SandboxScene::update(ecs::World& world, float aspectRatio, float deltaTime) {
    if (world.hasComponent<TransformComponent>(m_localPlayerEntity) &&
        world.hasComponent<CameraComponent>(m_cameraEntity)) {

        auto& playerTransform = world.getComponent<TransformComponent>(m_localPlayerEntity);
        auto& cam = world.getComponent<CameraComponent>(m_cameraEntity);

        if (world.hasComponent<LocalPlayerPredictionComponent>(m_localPlayerEntity)) {
            auto& pred = world.getComponent<LocalPlayerPredictionComponent>(m_localPlayerEntity);
            auto& rb = world.getComponent<RigidBodyComponent>(m_localPlayerEntity);

            float decayFactor = std::exp(-25.0f * deltaTime);
            pred.visualErrorOffset *= decayFactor;
            if (glm::dot(pred.visualErrorOffset, pred.visualErrorOffset) < 0.00001f) {
                pred.visualErrorOffset = glm::vec3(0.0f);
            }

            b3Vec3 physPos = b3Body_GetPosition(rb.bodyId);

            playerTransform.position = glm::vec3(physPos.x, physPos.y, physPos.z) + pred.visualErrorOffset;
            
        }

        cam.target = playerTransform.position;
    }

    CameraSystem::update(world, aspectRatio);
}

void SandboxScene::render(ecs::World& world, VkCommandBuffer cmd, VulkanContext& context) {
    RenderSystem::recordCommands(world, cmd, m_pipelineLayout, m_graphicsPipeline, m_cameraEntity);
}

void SandboxScene::shutdown(ecs::World& world, VulkanContext& context) {
    VkDevice dev = context.getDevice();
    auto& mem = context.getMemoryManager();

    if (world.hasSingleton<physics::PhysicsWorldSingleton>()) {
        world.getSingleton<physics::PhysicsWorldSingleton>().shutdown();
    }

    if (world.hasComponent<MeshComponent>(m_planeEntity)) {
        auto& m = world.getComponent<MeshComponent>(m_planeEntity);
        mem.freeGeometry(m.vertexAlloc);
        mem.freeGeometry(m.indexAlloc);
    }

    mem.freeGeometry(m_sharedCapsuleMesh.vertexAlloc);
    mem.freeGeometry(m_sharedCapsuleMesh.indexAlloc);

    mem.destroyImage(m_gridTexture);
    mem.destroyImage(m_localPlayerTexture);
    mem.destroyImage(m_remotePlayerTexture);

    if (m_graphicsPipeline) {
        vkDestroyPipeline(dev, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
}

} // namespace irox