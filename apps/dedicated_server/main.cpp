#include "engine/ecs/ECS.h"
#include "engine/physics/PhysicsWorld.h"
#include "engine/physics/PhysicsFactory.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/NetworkPackets.h"
#include "engine/network/ClientInputBuffer.h"
#include "game/shared/components/TransformComponent.h"
#include "game/shared/components/RigidBodyComponent.h"
#include "game/shared/components/CharacterControllerComponent.h"
#include "game/shared/components/CapsuleColliderComponent.h"
#include "game/shared/systems/CharacterMovementSystem.h"
#include "game/shared/systems/PhysicsSystem.h"
#include <chrono>
#include <thread>
#include <unordered_map>
#include <iostream>

struct ConnectedClient {
    uint32_t networkId = 0;
    irox::ecs::Entity entity = irox::ecs::NULL_ENTITY;
    irox::net::ClientInputBuffer inputBuffer {};

    std::unordered_map<uint32_t, float> entityPriorities;
};

float CalculatePriorityDelta(
    const glm::vec3& clientPos,
    float clientYawDeg,
    const glm::vec3& entityPos,
    bool isProjectile
) {
    if (isProjectile) {
        return 1.0f;
    }

    const float dist = glm::distance(clientPos, entityPos);

    if (dist < 20.0f) {
        return 1.0f;
    }

    // Directional Dot Product
    const float rad = glm::radians(clientYawDeg);
    const glm::vec3 clientFwd(-std::sin(rad), 0.0f, -std::cos(rad));
    const glm::vec3 toEntity = glm::normalize(entityPos - clientPos);
    const float dot = glm::dot(clientFwd, toEntity);

    const bool isInView = (dot > 0.2f); // ~75 degree field of view

    if (isInView && dist < 45.0f) {
        return 1.0f; // Visible and medium range: 60 Hz
    } else if (dist < 60.0f) {
        return 0.334f; // Distant or behind back: ~20 Hz
    } else {
        return 0.200f; // Far away: ~12 Hz
    }
}


int main(int argc, char** argv) {
    uint16_t port = 7777;
    std::cout << "[DedicatedServer] Starting on UDP port " << port << " with Input Buffering...\n";

    irox::ecs::World world;
    world.reserveEntities(2048);
    world.setComponentBudget<irox::TransformComponent>(2048);
    world.setComponentBudget<irox::RigidBodyComponent>(1024);
    world.setComponentBudget<irox::CharacterControllerComponent>(64);
    world.setComponentBudget<irox::CapsuleColliderComponent>(64);

    auto& physics = world.setSingleton<irox::physics::PhysicsWorldSingleton>();
    physics.init(-18.0f);

    irox::physics::PhysicsFactory::createStaticGroundBox(
        physics.worldId, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(25.0f, 0.5f, 25.0f)
    );

    irox::net::NetworkManager net;
    net.init();
    if (!net.startServer(port)) {
        std::cerr << "[DedicatedServer] Failed to bind server socket!\n";
        return -1;
    }

    std::unordered_map<HSteamNetConnection, ConnectedClient> clients;
    uint32_t nextNetworkId = 1;

    net.onClientConnected = [&](HSteamNetConnection conn) {
        uint32_t netId = nextNetworkId++;

        auto playerEntity = world.createEntity();
        world.addComponent<irox::TransformComponent>(playerEntity, irox::TransformComponent { .position = { 0.0f, 3.0f, 0.0f } });
        world.addComponent<irox::CapsuleColliderComponent>(playerEntity, irox::CapsuleColliderComponent { .radius = 0.4f, .halfHeight = 0.5f });
        world.addComponent<irox::CharacterControllerComponent>(playerEntity, irox::CharacterControllerComponent {});

        auto rb = irox::physics::PhysicsFactory::createCharacterCapsule(physics.worldId, glm::vec3(0.0f, 3.0f, 0.0f), 0.4f, 0.5f);
        world.addComponent<irox::RigidBodyComponent>(playerEntity, rb);

        clients[conn] = ConnectedClient {
            .networkId = netId,
            .entity = playerEntity
        };

        irox::net::ServerAcceptPacket acceptPacket { .assignedNetworkId = netId };
        net.sendPacket(conn, &acceptPacket, sizeof(acceptPacket), true);
        std::cout << "[DedicatedServer] Client connected: NetID=" << netId << "\n";
    };

    net.onClientDisconnected = [&](HSteamNetConnection conn) {
        if (clients.contains(conn)) {
            std::cout << "[DedicatedServer] Client disconnected: NetID=" << clients[conn].networkId << "\n";
            world.destroyEntity(clients[conn].entity);
            clients.erase(conn);
        }
    };

    constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
    uint32_t serverTick = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();
    float timeAccumulator = 0.0f;

    while (true) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float delta = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        timeAccumulator += delta;

        net.poll([&](HSteamNetConnection conn, const uint8_t* data, size_t size) {
            if (size < sizeof(irox::net::PacketType)) return;
            auto type = *reinterpret_cast<const irox::net::PacketType*>(data);

            if (type == irox::net::PacketType::ClientInput) {
                constexpr size_t headerAndSubtickSize = sizeof(irox::net::PacketType) + 3 + (sizeof(irox::net::SubtickEvent) * 4); // 20 bytes
                if (size < headerAndSubtickSize) return;

                const auto* packet = reinterpret_cast<const irox::net::ClientInputPacket*>(data);

                // Verify buffer actually contains reported cmdCount
                const size_t expectedMinSize = headerAndSubtickSize + (packet->cmdCount * sizeof(irox::net::UserCmd));
                if (size < expectedMinSize) return;

                if (clients.contains(conn)) {
                    clients[conn].inputBuffer.pushPacket(*packet);
                }
            } else if (type == irox::net::PacketType::GameEvent && size >= sizeof(irox::net::GameEventPacket)) {
                const auto* clientEv = reinterpret_cast<const irox::net::GameEventPacket*>(data);

                if (clients.contains(conn)) {
                    irox::net::GameEventPacket broadcastEv = *clientEv;
                    broadcastEv.senderNetworkId = clients[conn].networkId;

                    std::cout << "[Server] Relaying Event: Type=" << (int) broadcastEv.eventType
                        << " ID=" << (int) broadcastEv.eventId
                        << " from NetID=" << broadcastEv.senderNetworkId << "\n";

                    for (auto& [otherConn, otherClient] : clients) {
                        if (otherConn != conn) {
                            net.sendPacket(otherConn, &broadcastEv, sizeof(broadcastEv), true);
                        }
                    }
                }
            }
        });

        // 60 Hz Simulation
        while (timeAccumulator >= FIXED_TIMESTEP) {

            // Process Player Commands with Catch-Up
            for (auto& [conn, client] : clients) {
                if (client.entity == irox::ecs::NULL_ENTITY) continue;

                auto& rb = world.getComponent<irox::RigidBodyComponent>(client.entity);
                auto& controller = world.getComponent<irox::CharacterControllerComponent>(client.entity);

                if (client.inputBuffer.highestReceivedTick > client.inputBuffer.lastProcessedTick + irox::net::ClientInputBuffer::MAX_ACCUMULATED_TICKS) {
                    client.inputBuffer.lastProcessedTick = client.inputBuffer.highestReceivedTick - irox::net::ClientInputBuffer::MAX_ACCUMULATED_TICKS;
                }

                // Catch-up simulation (up to 4 commands per frame)
                uint32_t simulatedTicks = 0;
                while (client.inputBuffer.hasNext() && simulatedTicks < irox::net::ClientInputBuffer::MAX_CATCHUP_PER_FRAME) {
                    irox::net::UserCmd cmd = client.inputBuffer.popNext();
                    simulatedTicks++;

                    glm::vec3 moveDir(
                        irox::net::UserCmd::unpackAxis(cmd.moveX),
                        0.0f,
                        irox::net::UserCmd::unpackAxis(cmd.moveZ)
                    );
                    bool jump = (cmd.actions & irox::net::Action_Jump) != 0;

                    if (client.inputBuffer.hasNext() && simulatedTicks < irox::net::ClientInputBuffer::MAX_CATCHUP_PER_FRAME) {
                        // Displace body position to catch up
                        irox::CharacterMovementSystem::catchUpStep(rb, controller, moveDir, jump, FIXED_TIMESTEP);
                    } else {
                        // Set velocity for Box3D to step in the physics world
                        // todo: Implement proper catch-up steps for physics engine
                        irox::CharacterMovementSystem::step(rb, controller, moveDir, jump);
                    }
                }
            }

            irox::PhysicsSystem::update(world, FIXED_TIMESTEP);

            for (auto& [targetConn, targetClient] : clients) {
                if (targetClient.entity == irox::ecs::NULL_ENTITY) continue;

                irox::net::WorldSnapshotPacket snapshot {};
                snapshot.serverTick = serverTick;
                snapshot.acknowledgedClientTick = targetClient.inputBuffer.lastProcessedTick;
                snapshot.entityCount = 0;

                for (auto& [otherConn, otherClient] : clients) {
                    if (otherClient.entity == irox::ecs::NULL_ENTITY) continue;
                    if (snapshot.entityCount >= 16) break;

                    auto& otherTf = world.getComponent<irox::TransformComponent>(otherClient.entity);
                    auto& otherRb = world.getComponent<irox::RigidBodyComponent>(otherClient.entity);
                    auto& otherCc = world.getComponent<irox::CharacterControllerComponent>(otherClient.entity);
                    b3Vec3 vel = b3Body_GetLinearVelocity(otherRb.bodyId);

                    snapshot.entities[snapshot.entityCount++] = irox::net::EntitySnapshotData {
                        .networkId = otherClient.networkId,
                        .posX = otherTf.position.x,
                        .posY = otherTf.position.y,
                        .posZ = otherTf.position.z,
                        .velX = vel.x,
                        .velY = vel.y,
                        .velZ = vel.z,
                        .yaw = otherClient.inputBuffer.queue[otherClient.inputBuffer.lastProcessedTick % irox::net::ClientInputBuffer::QUEUE_CAPACITY].cmd.yaw,
                        .type = irox::net::EntityNetType::Player,
                        .isGrounded = static_cast<uint8_t>(otherCc.isGrounded ? 1 : 0)
                    };
                }

                const size_t packetSize = sizeof(irox::net::PacketType) + 3 + (3 * sizeof(uint32_t))
                    + (snapshot.entityCount * sizeof(irox::net::EntitySnapshotData));
                net.sendPacket(targetConn, &snapshot, packetSize, false);
            }

            serverTick++;
            timeAccumulator -= FIXED_TIMESTEP;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}