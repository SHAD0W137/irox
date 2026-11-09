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

int main(int argc, char** argv) {
    irox::NetworkMode netMode = irox::NetworkMode::Client;
    std::string serverIp = "127.0.0.1";
    uint16_t port = 7777;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--singleplayer") {
            netMode = irox::NetworkMode::Singleplayer;
        } else if (arg == "--host") {
            netMode = irox::NetworkMode::ListenServer;
        } else {
            serverIp = arg;
        }
    }

    std::cout << "[irox] Running in Mode: "
        << (netMode == irox::NetworkMode::Client ? "Multiplayer Client" :
           (netMode == irox::NetworkMode::ListenServer ? "Listen Server (Host)" : "Singleplayer"))
        << "\n";

    irox::VulkanContext gfx;
    gfx.init(1280, 720, "irox");

    irox::ecs::World world;
    auto& input = world.setSingleton<irox::InputState>();

    irox::net::NetworkManager net;
    if (netMode == irox::NetworkMode::Client) {
        net.init();
        net.connectToServer(serverIp.c_str(), port);
    } else if (netMode == irox::NetworkMode::ListenServer) {
        net.init();
        net.startServer(port);
    }

    irox::SandboxScene scene;
    scene.init(world, gfx, netMode);

    bool running = true;
    SDL_Event event;
    bool appIsActive = true;

    constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    float timeAccumulator = 0.0f;
    uint32_t currentTick = 0;
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

            if (event.type == SDL_EVENT_DID_ENTER_BACKGROUND) appIsActive = false;
            else if (event.type == SDL_EVENT_WILL_ENTER_FOREGROUND) { appIsActive = true; gfx.onWindowResize(); }

            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_RESTORED) {
                gfx.onWindowResize();
            }

            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) input.isLeftMouseDown = true;
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT) input.isLeftMouseDown = false;
            if (event.type == SDL_EVENT_MOUSE_MOTION) input.mouseDelta = { event.motion.xrel, event.motion.yrel };
            if (event.type == SDL_EVENT_MOUSE_WHEEL) input.mouseWheel = event.wheel.y;

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

            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_V && net.isConnected()) {
                    const uint32_t myNetId = scene.getLocalNetworkId();

                    irox::net::GameEventPacket packet {
                        .type = irox::net::PacketType::GameEvent,
                        .eventType = irox::net::GameEventType::VoiceLine,
                        .eventId = 1, // Voice line or some emote
                        .senderNetworkId = myNetId
                    };
                    net.sendPacket(net.getServerConnection(), &packet, sizeof(packet), true);

                    SDL_Log("[Local Player] Voice Line 1");
                }
            }
        }

        if (netMode != irox::NetworkMode::Singleplayer) {
            net.poll([&](HSteamNetConnection conn, const uint8_t* data, size_t size) {
                if (size < sizeof(irox::net::PacketType)) return;
                auto type = *reinterpret_cast<const irox::net::PacketType*>(data);

                if (type == irox::net::PacketType::ServerAccept && size >= sizeof(irox::net::ServerAcceptPacket)) {
                    auto accept = *reinterpret_cast<const irox::net::ServerAcceptPacket*>(data);
                    SDL_Log("[Client] Accepted! NetID=%u", accept.assignedNetworkId);
                    scene.setLocalNetworkId(accept.assignedNetworkId);
                }
                else if (type == irox::net::PacketType::WorldSnapshot) {
                    constexpr size_t minHeaderSize = sizeof(irox::net::PacketType) + 3 + (3 * sizeof(uint32_t)); // 16 bytes
                    if (size >= minHeaderSize) {
                        const auto* packet = reinterpret_cast<const irox::net::WorldSnapshotPacket*>(data);
                        const size_t expectedMinSize = minHeaderSize + (packet->entityCount * sizeof(irox::net::EntitySnapshotData));

                        if (size >= expectedMinSize) {
                            scene.applyWorldSnapshot(world, gfx, *packet);
                        }
                    }
                }

                if (type == irox::net::PacketType::GameEvent && size >= sizeof(irox::net::GameEventPacket)) {
                    const auto* ev = reinterpret_cast<const irox::net::GameEventPacket*>(data);

                    if (ev->eventType == irox::net::GameEventType::VoiceLine) {
                        SDL_Log("[Remote Voice Line] Player NetID=%u shouted Voice Line %u",
                                ev->senderNetworkId, ev->eventId);
                    } else if (ev->eventType == irox::net::GameEventType::Emote) {
                        SDL_Log("[Remote Emote] Player NetID=%u started Emote %u",
                                ev->senderNetworkId, ev->eventId);
                    }
                }
            });
        }

        if (!appIsActive || gfx.isMinimized()) {
            SDL_Delay(32);
            lastTime = std::chrono::high_resolution_clock::now();
            continue;
        }

        // Fixed Timestep Loop
        timeAccumulator += frameDeltaTime;
        while (timeAccumulator >= FIXED_TIMESTEP) {

            // Camera-relative movement
            glm::vec3 dir(0.0f);
            float currentCameraYaw = 0.0f;
            auto camView = world.View<irox::CameraComponent>();
            for (auto camEntity : camView) {
                auto& cam = camView.get<irox::CameraComponent>(camEntity);
                currentCameraYaw = cam.yaw;
                glm::quat camWorldRot = glm::conjugate(cam.orientation);

                glm::vec3 rawFwd = camWorldRot * glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 rawRgt = camWorldRot * glm::vec3(1.0f, 0.0f, 0.0f);

                glm::vec3 fwd = glm::normalize(glm::vec3(rawFwd.x, 0.0f, rawFwd.z));
                glm::vec3 rgt = glm::normalize(glm::vec3(rawRgt.x, 0.0f, rawRgt.z));

                if (input.keyW) dir += fwd;
                if (input.keyS) dir -= fwd;
                if (input.keyD) dir += rgt;
                if (input.keyA) dir -= rgt;

                if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);
                break;
            }

            const bool jumpRequested = (pendingJump || input.keySpace);
            pendingJump = false;

            uint32_t actionMask = irox::net::Action_None;
            if (jumpRequested) actionMask |= irox::net::Action_Jump;

            irox::net::UserCmd cmd {
                .tick = currentTick,
                .moveX = irox::net::UserCmd::packAxis(dir.x),
                .moveZ = irox::net::UserCmd::packAxis(dir.z),
                .yaw = irox::net::UserCmd::packAngle(glm::degrees(currentCameraYaw)),
                .actions = actionMask
            };

            auto localPlayer = scene.getLocalPlayerEntity();

            if (world.hasComponent<irox::PlayerInputComponent>(localPlayer)) {
                auto& pInput = world.getComponent<irox::PlayerInputComponent>(localPlayer);
                pInput.moveDir = dir;
                pInput.jumpRequested = jumpRequested;
            }

            // Client-Side Prediction
            if (netMode == irox::NetworkMode::Client && world.hasComponent<irox::LocalPlayerPredictionComponent>(localPlayer)) {
                auto& pred = world.getComponent<irox::LocalPlayerPredictionComponent>(localPlayer);
                pred.currentClientTick = currentTick;

                const size_t slot = currentTick % irox::LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                pred.inputHistory[slot] = cmd;

                if (net.isConnected()) {
                    // All unacknowledged commands (up to 16)
                    uint32_t startTick = pred.lastAcknowledgedServerTick + 1;

                    // Startup safeguard before the first server snapshot arrives
                    // todo: provide better solution
                    if (pred.lastAcknowledgedServerTick == 0) {
                        startTick = (currentTick > irox::net::MAX_PACKET_COMMANDS)
                            ? (currentTick - irox::net::MAX_PACKET_COMMANDS + 1)
                            : 1;
                    }

                    // Max 16 commands
                    if (currentTick >= startTick + irox::net::MAX_PACKET_COMMANDS) {
                        startTick = currentTick - irox::net::MAX_PACKET_COMMANDS + 1;
                    }

                    irox::net::ClientInputPacket packet {};
                    packet.type = irox::net::PacketType::ClientInput;
                    packet.cmdCount = 0;
                    packet.subtickCount = 0;

                    for (uint32_t t = startTick; t <= currentTick; ++t) {
                        const size_t hSlot = t % irox::LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                        packet.commands[packet.cmdCount++] = pred.inputHistory[hSlot];
                    }

                    // Dynamic byte size
                    // Header (4B) + Subticks (16B) + exactly (cmdCount * 16B)
                    const size_t dynamicPacketSize = sizeof(irox::net::PacketType) + 3
                        + sizeof(packet.subticks)
                        + (packet.cmdCount * sizeof(irox::net::UserCmd));

                    net.sendPacket(net.getServerConnection(), &packet, dynamicPacketSize, false);
                }
            }

            scene.fixedUpdate(world, FIXED_TIMESTEP);

            if (netMode == irox::NetworkMode::Client &&
                world.hasComponent<irox::LocalPlayerPredictionComponent>(localPlayer) &&
                world.hasComponent<irox::RigidBodyComponent>(localPlayer)) {

                auto& pred = world.getComponent<irox::LocalPlayerPredictionComponent>(localPlayer);
                auto& rb = world.getComponent<irox::RigidBodyComponent>(localPlayer);

                b3Vec3 pos = b3Body_GetPosition(rb.bodyId);
                b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);

                const size_t slot = currentTick % irox::LocalPlayerPredictionComponent::HISTORY_CAPACITY;
                pred.stateHistory[slot] = irox::PredictedState {
                    .tick = currentTick,
                    .position = { pos.x, pos.y, pos.z },
                    .velocity = { vel.x, vel.y, vel.z },
                    .isGrounded = (std::abs(vel.y) < 0.05f)
                };
            }

            // Host / Listen-Server: Broadcast snapshots at 60 Hz
            if (netMode == irox::NetworkMode::ListenServer) {
                irox::net::WorldSnapshotPacket packet {
                    .serverTick = currentTick,
                    .acknowledgedClientTick = 0,
                    .entityCount = 0
                };

                auto view = world.View<irox::TransformComponent, irox::RigidBodyComponent>();
                for (auto e : view) {
                    if (packet.entityCount >= 16) break;
                    auto& tf = view.get<irox::TransformComponent>(e);
                    auto& rb = view.get<irox::RigidBodyComponent>(e);
                    b3Vec3 vel = b3Body_GetLinearVelocity(rb.bodyId);

                    // Fill entity directly inside packet
                    packet.entities[packet.entityCount++] = irox::net::EntitySnapshotData {
                        .networkId = static_cast<uint32_t>(e),
                        .posX = tf.position.x, .posY = tf.position.y, .posZ = tf.position.z,
                        .velX = vel.x, .velY = vel.y, .velZ = vel.z,
                        .yaw = irox::net::UserCmd::packAngle(0.0f),
                        .isGrounded = static_cast<uint8_t>(std::abs(vel.y) < 0.05f)
                    };
                }

                net.broadcastSnapshot(packet);
            }

            currentTick++;
            timeAccumulator -= FIXED_TIMESTEP;
        }

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
    if (netMode != irox::NetworkMode::Singleplayer) {
        net.shutdown();
    }
    gfx.shutdown();
    return 0;
}