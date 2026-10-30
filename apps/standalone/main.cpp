#include "engine/render/VulkanContext.h"
#include "engine/ecs/ECS.h"
#include "engine/input/InputState.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/NetworkPackets.h"
#include "game/scenes/SandboxScene.h"
#include "game/client/components/CameraComponent.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/PlayerInputComponent.h"
#include "game/shared/components/PredictionComponent.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <chrono>
#include <iostream>
#include <string>
#include <algorithm>

constexpr bool ENABLE_PREDICTION = true;

int main(int argc, char** argv) {
    std::string serverIp = "127.0.0.1";
    if (argc > 1) {
        serverIp = argv[1];
    } else {
        size_t ipFileSize = 0;
        void* ipData = SDL_LoadFile("server_ip.txt", &ipFileSize);
        if (ipData) {
            std::string fileContent(static_cast<char*>(ipData), ipFileSize);
            fileContent.erase(fileContent.find_last_not_of(" \n\r\t") + 1);
            if (!fileContent.empty()) serverIp = fileContent;
            SDL_free(ipData);
        }
    }

    std::cout << "[irox] Target Server IP: " << serverIp << "\n";

    irox::VulkanContext gfx;
    gfx.init(1280, 720, "irox");

    irox::ecs::World world;
    auto& input = world.setSingleton<irox::InputState>();

    irox::net::NetworkManager net;
    net.init();
    net.connectToServer(serverIp.c_str(), 7777);

    irox::SandboxScene scene;
    scene.init(world, gfx);

    bool running = true;
    SDL_Event event;
    bool appIsActive = true;

    constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    float timeAccumulator = 0.0f;
    uint32_t clientTick = 0;
    bool pendingJump = false;

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (running) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float frameDeltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (frameDeltaTime > 0.25f) frameDeltaTime = 0.25f;

        input.mouseDelta = { 0.0f, 0.0f };
        input.mouseWheel = 0.0f;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;

            if (event.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
                appIsActive = false;
            } else if (event.type == SDL_EVENT_WILL_ENTER_FOREGROUND) {
                appIsActive = true;
                gfx.onWindowResize();
            }

            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_RESTORED) {
                gfx.onWindowResize();
            }

            // Мouse
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (event.button.button == SDL_BUTTON_LEFT) input.isLeftMouseDown = true;
            } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.button.button == SDL_BUTTON_LEFT) input.isLeftMouseDown = false;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
                input.mouseDelta = { event.motion.xrel, event.motion.yrel };
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                input.mouseWheel = event.wheel.y;
            }

            // Sensor input (Android)
            if (event.type == SDL_EVENT_FINGER_DOWN) {
                input.isLeftMouseDown = true;
            } else if (event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED) {
                input.isLeftMouseDown = false;
            } else if (event.type == SDL_EVENT_FINGER_MOTION) {
                float w = static_cast<float>(gfx.getSwapchainExtent().width);
                float h = static_cast<float>(gfx.getSwapchainExtent().height);
                input.mouseDelta.x = event.tfinger.dx * w;
                input.mouseDelta.y = event.tfinger.dy * h;
            }

            // Keyboard
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                bool isDown = (event.type == SDL_EVENT_KEY_DOWN);
                switch (event.key.key) {
                case SDLK_W: input.keyW = isDown; break;
                case SDLK_S: input.keyS = isDown; break;
                case SDLK_A: input.keyA = isDown; break;
                case SDLK_D: input.keyD = isDown; break;
                case SDLK_SPACE:
                    input.keySpace = isDown;
                    if (isDown) pendingJump = true;
                    break;
                }
            }
        }

        // Network: Receiving packets
        net.poll([&](HSteamNetConnection conn, const uint8_t* data, size_t size) {
            if (size < sizeof(irox::net::PacketType)) return;
            auto type = *reinterpret_cast<const irox::net::PacketType*>(data);

            if (type == irox::net::PacketType::ServerAccept && size >= sizeof(irox::net::ServerAcceptPacket)) {
                auto accept = *reinterpret_cast<const irox::net::ServerAcceptPacket*>(data);
                SDL_Log("[Client] Accepted by server! My NetID = %u", accept.assignedNetworkId);
                scene.setLocalNetworkId(accept.assignedNetworkId);
            } else if (type == irox::net::PacketType::WorldSnapshot && size >= (sizeof(irox::net::WorldSnapshotPacket) - sizeof(irox::net::EntitySnapshotData) * 16)) {
                auto snapshot = *reinterpret_cast<const irox::net::WorldSnapshotPacket*>(data);
                scene.applyWorldSnapshot(world, gfx, snapshot);
            }
        });

        if (!appIsActive || gfx.isMinimized()) {
            SDL_Delay(32);
            lastTime = std::chrono::high_resolution_clock::now();
            continue;
        }

        // Fixed tick cycle (prediction and simulation)
        timeAccumulator += frameDeltaTime;
        while (timeAccumulator >= FIXED_TIMESTEP) {

            // Camera-realtive directional vector
            glm::vec3 dir(0.0f);
            float currentCameraYaw = 0.0f;
            auto& camPool = world.getPool<irox::CameraComponent>();
            for (auto camEntity : camPool.entities()) {
                auto& cam = camPool.get(camEntity);
                glm::quat camWorldRot = glm::conjugate(cam.orientation);

                glm::vec3 rawFwd = camWorldRot * glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 rawRgt = camWorldRot * glm::vec3(1.0f, 0.0f, 0.0f);

                glm::vec3 fwd = glm::vec3(rawFwd.x, 0.0f, rawFwd.z);
                glm::vec3 rgt = glm::vec3(rawRgt.x, 0.0f, rawRgt.z);

                if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
                if (glm::length(rgt) > 0.001f) rgt = glm::normalize(rgt);

                if (input.keyW) dir += fwd;
                if (input.keyS) dir -= fwd;
                if (input.keyD) dir += rgt;
                if (input.keyA) dir -= rgt;

                if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);
                break;
            }

            const bool jumpRequested = (pendingJump || input.keySpace);
            pendingJump = false;

            // Local input prediction and writing to ringbuffer
            auto localPlayer = scene.getLocalPlayerEntity();
            if (world.hasComponent<irox::LocalPlayerPredictionComponent>(localPlayer)) {
                auto& pred = world.getComponent<irox::LocalPlayerPredictionComponent>(localPlayer);
                pred.currentClientTick = clientTick;

                const size_t slot = clientTick % irox::LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                pred.inputHistory[slot] = irox::PredictedInput {
                    .tick = clientTick,
                    .moveDir = dir,
                    .jumpRequested = jumpRequested
                };

                // Recording to PlayerInputComponent for local step
                if (world.hasComponent<irox::PlayerInputComponent>(localPlayer)) {
                    auto& pInput = world.getComponent<irox::PlayerInputComponent>(localPlayer);
                    pInput.moveDir = dir;
                    pInput.jumpRequested = jumpRequested;
                }
            }

            // Input packet sent to server
            if (net.isConnected()) {
                irox::net::ClientInputPacket inputPacket;
                inputPacket.clientTick = clientTick;
                inputPacket.moveDirX = dir.x;
                inputPacket.moveDirY = dir.y;
                inputPacket.moveDirZ = dir.z;
                inputPacket.cameraYaw = currentCameraYaw;

                if (jumpRequested) {
                    inputPacket.addSubtick(irox::net::SubtickAction::Jump, 0.0f);
                }

                net.sendPacket(net.getServerConnection(), &inputPacket, sizeof(inputPacket), false);
            }


            // Client-Side Prediction step
            if (ENABLE_PREDICTION) {
                scene.fixedUpdate(world, FIXED_TIMESTEP);
            }

            // Current state save to ringbuffer
            if (world.hasComponent<irox::LocalPlayerPredictionComponent>(localPlayer) &&
                world.hasComponent<irox::RigidBodyComponent>(localPlayer)) {

                auto& pred = world.getComponent<irox::LocalPlayerPredictionComponent>(localPlayer);
                auto& rb = world.getComponent<irox::RigidBodyComponent>(localPlayer);

                b3Vec3 pos = b3Body_GetPosition(rb.bodyId);
                b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);

                const size_t slot = clientTick % irox::LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                pred.stateHistory[slot] = irox::PredictedState {
                    .tick = clientTick,
                    .position = { pos.x, pos.y, pos.z },
                    .velocity = { vel.x, vel.y, vel.z },
                    .isGrounded = (std::abs(vel.y) < 0.05f)
                };
            }

            clientTick++;
            timeAccumulator -= FIXED_TIMESTEP;
        }

        // Rendering and camera update with error smoothing
        scene.update(world, gfx.getAspectRatio(), frameDeltaTime);

        uint32_t imageIndex;
        if (gfx.beginFrame(imageIndex)) {
            gfx.recordFrame(imageIndex, [&](VkCommandBuffer cmd) {
                scene.render(world, cmd, gfx);
            });
            gfx.endFrame(imageIndex);
        }
    }

    gfx.waitIdle();
    scene.shutdown(world, gfx);
    net.shutdown();
    gfx.shutdown();
    return 0;
}