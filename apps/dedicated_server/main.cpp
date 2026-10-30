#include "engine/ecs/ECS.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/NetworkPackets.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/PhysicsFactory.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/CapsuleColliderComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include "game/shared/components/PlayerInputComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/systems/CharacterMovementSystem.h"
#include "game/shared/systems/PhysicsSystem.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <map>

struct ClientSession {
    irox::ecs::Entity entity = irox::ecs::NULL_ENTITY;
    uint32_t networkId = 0;
    uint32_t lastClientTick = 0;
    std::map<uint32_t, irox::net::ClientInputPacket> inputQueue;
};

int main() {
    std::cout << "  irox Dedicated Server (Box3D Physics Engine)\n";

    irox::net::NetworkManager net;
    if (!net.init()) return -1;
    if (!net.startServer(7777)) return -1;

    irox::ecs::World world;

    // Physical world init
    auto& physics = world.setSingleton<irox::physics::PhysicsWorldSingleton>();
    physics.init(-18.0f); // Gravity

    // Static physical floor
    irox::ecs::Entity groundEntity = world.createEntity();
    auto groundRb = irox::physics::PhysicsFactory::createStaticGroundBox(
        physics.worldId,
        glm::vec3(0.0f, -0.5f, 0.0f),
        glm::vec3(25.0f, 0.5f, 25.0f)
    );
    world.addComponent<irox::RigidBodyComponent>(groundEntity, groundRb);

    std::unordered_map<HSteamNetConnection, ClientSession> clients;
    uint32_t nextNetworkId = 1;

    // New client connection
    net.onClientConnected = [&](HSteamNetConnection conn) {
        uint32_t netId = nextNetworkId++;
        float spawnZ = (netId % 2 == 1) ? -2.5f : 2.5f;

        irox::ecs::Entity player = world.createEntity();
        world.addComponent<irox::TransformComponent>(player, irox::TransformComponent { .position = { 0.0f, 3.0f, spawnZ } });
        world.addComponent<irox::CapsuleColliderComponent>(player, irox::CapsuleColliderComponent { .radius = 0.4f, .halfHeight = 0.5f });
        world.addComponent<irox::CharacterControllerComponent>(player, irox::CharacterControllerComponent {});
        world.addComponent<irox::PlayerInputComponent>(player, irox::PlayerInputComponent {});

        auto capsuleRb = irox::physics::PhysicsFactory::createCharacterCapsule(
            physics.worldId,
            glm::vec3(0.0f, 3.0f, spawnZ),
            0.4f, 0.5f
        );
        world.addComponent<irox::RigidBodyComponent>(player, capsuleRb);

        clients[conn] = { .entity = player, .networkId = netId, .lastClientTick = 0 };

        irox::net::ServerAcceptPacket accept;
        accept.assignedNetworkId = netId;
        net.sendPacket(conn, &accept, sizeof(accept), true);

        std::cout << "[Server] Spawned Box3D Player (NetID=" << netId << ", Z=" << spawnZ << ")\n";
    };

    // Disconnecting client
    net.onClientDisconnected = [&](HSteamNetConnection conn) {
        if (clients.contains(conn)) {
            auto& session = clients[conn];
            std::cout << "[Server] Despawned Player (NetID=" << session.networkId << ")\n";

            if (world.hasComponent<irox::RigidBodyComponent>(session.entity)) {
                b3DestroyBody(world.getComponent<irox::RigidBodyComponent>(session.entity).bodyId);
            }

            world.destroyEntity(session.entity);
            clients.erase(conn);
        }
    };

    constexpr float FIXED_DT = 1.0f / 60.0f;
    uint32_t serverTick = 0;
    bool running = true;
    auto nextTickTime = std::chrono::high_resolution_clock::now();

    while (running) {
        // Input packets from clients
        // Input accumulated to a queue
        net.poll([&](HSteamNetConnection conn, const uint8_t* data, size_t size) {
            if (size < sizeof(irox::net::PacketType)) return;
            auto type = *reinterpret_cast<const irox::net::PacketType*>(data);

            if (type == irox::net::PacketType::ClientInput && size >= sizeof(irox::net::ClientInputPacket)) {
                if (!clients.contains(conn)) return;
                const auto& inputPacket = *reinterpret_cast<const irox::net::ClientInputPacket*>(data);

                // Enqueue packet by tock number
                clients[conn].inputQueue[inputPacket.clientTick] = inputPacket;
            }
        });

        // Before simulation calculate just one packet
        for (auto& [conn, session] : clients) {
            if (!session.inputQueue.empty()) {
                // Drop packets when lag spike
                while (session.inputQueue.size() > 5) {
                    session.inputQueue.erase(session.inputQueue.begin());
                }

                // Getting old oredered tick
                auto it = session.inputQueue.begin();
                const auto& input = it->second;

                if (world.hasComponent<irox::PlayerInputComponent>(session.entity)) {
                    auto& pi = world.getComponent<irox::PlayerInputComponent>(session.entity);
                    pi.moveDir = glm::vec3(input.moveDirX, input.moveDirY, input.moveDirZ);
                    for (uint32_t i = 0; i < input.subtickCount; ++i) {
                        if (input.subticks[i].action == irox::net::SubtickAction::Jump) pi.jumpRequested = true;
                    }
                }

                session.lastClientTick = input.clientTick;
                session.inputQueue.erase(it);
            }
        }


        // Determined 60hz physics
        irox::CharacterMovementSystem::update(world, FIXED_DT);
        irox::PhysicsSystem::update(world, FIXED_DT);

        // Sending snapshots (Unicast for every client with acknowledgedClientTick)
        for (const auto& [conn, session] : clients) {
            irox::net::WorldSnapshotPacket snapshot;
            snapshot.serverTick = serverTick;
            snapshot.acknowledgedClientTick = session.lastClientTick;
            snapshot.entityCount = 0;

            for (const auto& [targetConn, targetSession] : clients) {
                if (!world.hasComponent<irox::RigidBodyComponent>(targetSession.entity)) continue;

                // Data from physical engine
                auto& rb = world.getComponent<irox::RigidBodyComponent>(targetSession.entity);
                auto& cc = world.getComponent<irox::CharacterControllerComponent>(targetSession.entity);

                b3Vec3 physPos = b3Body_GetPosition(rb.bodyId);
                b3Vec3 physVel = b3Body_GetLinearVelocity(rb.bodyId);

                snapshot.entities[snapshot.entityCount++] = {
                    .networkId = targetSession.networkId,
                    .posX = physPos.x,
                    .posY = physPos.y,
                    .posZ = physPos.z,
                    .velX = physVel.x,
                    .velY = physVel.y,
                    .velZ = physVel.z,
                    .isGrounded = cc.isGrounded ? 1u : 0u
                };
            }

            net.sendPacket(conn, &snapshot, sizeof(snapshot), false);
        }

        serverTick++;
        nextTickTime += std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(
            std::chrono::duration<float>(FIXED_DT)
        );
        std::this_thread::sleep_until(nextTickTime);
    }

    physics.shutdown();
    net.shutdown();
    return 0;
}