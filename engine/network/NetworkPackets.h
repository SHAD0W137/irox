#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace irox::net {

enum class PacketType : uint8_t {
    ClientConnect = 1,
    ServerAccept = 2,
    ClientInput = 3,
    WorldSnapshot = 4,
    GameEvent = 5,
    ClientDisconnect = 6
};

enum class GameEventType : uint8_t {
    VoiceLine = 1,
    Emote = 2,
    PlaySound = 3
};

enum PlayerActionFlags : uint32_t {
    Action_None = 0,
    Action_Jump = 1 << 0,
    Action_PrimaryFire = 1 << 1,
    Action_SecondaryFire = 1 << 2,
    Action_Crouch = 1 << 3,
    Action_Sprint = 1 << 4,
};

inline constexpr uint8_t MAX_PACKET_COMMANDS = 16;

#pragma pack(push, 4)

struct SubtickEvent {
    uint8_t actionIndex = 0;
    uint8_t fraction = 0;
    uint8_t pad[2] = { 0, 0 };
};

// 16-byte packed User Command
struct UserCmd {
    uint32_t tick = 0;
    int8_t moveX = 0;
    int8_t moveZ = 0;
    uint16_t yaw = 0;
    uint16_t pitch = 0;
    uint16_t pad = 0;
    uint32_t actions = 0;

    static int8_t packAxis(float val) {
        return static_cast<int8_t>(std::clamp(val, -1.0f, 1.0f) * 127.0f);
    }
    static float unpackAxis(int8_t val) {
        return static_cast<float>(val) / 127.0f;
    }
    static uint16_t packAngle(float deg) {
        float norm = std::fmod(deg, 360.0f);
        if (norm < 0.0f) norm += 360.0f;
        return static_cast<uint16_t>((norm / 360.0f) * 65535.0f);
    }
    static float unpackAngle(uint16_t val) {
        return (static_cast<float>(val) / 65535.0f) * 360.0f;
    }
};

struct ClientInputPacket {
    PacketType type = PacketType::ClientInput;
    uint8_t cmdCount = 0;
    uint8_t subtickCount = 0;
    uint8_t pad = 0;

    SubtickEvent subticks[4];    
    UserCmd commands[MAX_PACKET_COMMANDS];
};

struct ServerAcceptPacket {
    PacketType type = PacketType::ServerAccept;
    uint8_t pad[3] = { 0, 0, 0 };
    uint32_t assignedNetworkId = 0;
};

enum class EntityNetType : uint8_t {
    Player = 1,
    Projectile = 2,
    Prop = 3
};

struct EntitySnapshotData {
    uint32_t networkId = 0;
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    uint16_t yaw = 0;
    EntityNetType type = EntityNetType::Player;
    uint8_t isGrounded = 0;
};

struct WorldSnapshotPacket {
    PacketType type = PacketType::WorldSnapshot;
    uint8_t pad[3] = { 0, 0, 0 };
    uint32_t serverTick = 0;
    uint32_t acknowledgedClientTick = 0;
    uint32_t entityCount = 0;
    EntitySnapshotData entities[16];
};

struct GameEventPacket {
    PacketType type = PacketType::GameEvent;
    GameEventType eventType = GameEventType::VoiceLine;
    uint8_t eventId = 0;
    uint8_t pad = 0;
    uint32_t senderNetworkId = 0;
};

#pragma pack(pop)

} // namespace irox::net