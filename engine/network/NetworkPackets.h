#pragma once
#include <cstdint>
#include <glm/glm.hpp>


namespace irox::net {

enum class PacketType : uint8_t {
    ClientConnect = 1,
    ServerAccept = 2,
    ClientInput = 3,
    WorldSnapshot = 4
};

// Cross-platform 4-byte alignment (for both x64 PC and ARM64 Android)
#pragma pack(push, 4)

enum class SubtickAction : uint8_t {
    Jump = 1,
    Shoot = 2
};

struct SubtickEvent {
    SubtickAction action;
    uint8_t padding[3] = { 0, 0, 0 };
    float fraction = 0.0f;
};

struct ClientInputPacket {
    PacketType type = PacketType::ClientInput;
    uint8_t padding[3] = { 0, 0, 0 };
    uint32_t clientTick = 0;
    float moveDirX = 0.0f;
    float moveDirY = 0.0f;
    float moveDirZ = 0.0f;
    float cameraYaw = 0.0f;
    uint32_t subtickCount = 0;
    SubtickEvent subticks[4];

    void addSubtick(SubtickAction action, float fraction) {
        if (subtickCount < 4) {
            subticks[subtickCount++] = { action, {0, 0, 0}, fraction };
        }
    }
};

struct ServerAcceptPacket {
    PacketType type = PacketType::ServerAccept;
    uint8_t padding[3] = { 0, 0, 0 };
    uint32_t assignedNetworkId = 0;
};

struct EntitySnapshotData {
    uint32_t networkId = 0;
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    uint32_t isGrounded = 0; // 1 = true, 0 = false
};

struct WorldSnapshotPacket {
    PacketType type = PacketType::WorldSnapshot;
    uint8_t padding[3] = { 0, 0, 0 };
    uint32_t serverTick = 0;
    uint32_t acknowledgedClientTick = 0;
    uint32_t entityCount = 0;
    EntitySnapshotData entities[16]; // До 16 игроков
};

#pragma pack(pop)

} // namespace irox::net