#pragma once
#include "engine/network/NetworkPackets.h"
#include <array>
#include <algorithm>
#include <cstdint>

namespace irox::net {

struct ClientInputBuffer {
    static constexpr size_t QUEUE_CAPACITY = 64;
    static constexpr uint32_t MAX_CATCHUP_PER_FRAME = 4;
    static constexpr uint32_t MAX_ACCUMULATED_TICKS = 20;

    struct Entry {
        UserCmd cmd {};
        bool valid { false };
    };

    std::array<Entry, QUEUE_CAPACITY> queue {};
    uint32_t lastProcessedTick { 0 };
    uint32_t highestReceivedTick { 0 };

    void push(const UserCmd& cmd) {
        if (lastProcessedTick == 0) {
            lastProcessedTick = cmd.tick - 1;
            highestReceivedTick = cmd.tick - 1;
        }
        if (cmd.tick <= lastProcessedTick) return;
        if (cmd.tick > lastProcessedTick + QUEUE_CAPACITY) return;

        const size_t slot = cmd.tick % QUEUE_CAPACITY;
        queue[slot].cmd = cmd;
        queue[slot].valid = true;

        if (cmd.tick > highestReceivedTick) {
            highestReceivedTick = cmd.tick;
        }
    }

    void pushPacket(const ClientInputPacket& packet) {
        const uint8_t count = std::min(packet.cmdCount, MAX_PACKET_COMMANDS);
        for (uint8_t i = 0; i < count; ++i) {
            push(packet.commands[i]);
        }
    }

    [[nodiscard]] bool hasNext() const noexcept {
        const uint32_t nextTick = lastProcessedTick + 1;
        const size_t slot = nextTick % QUEUE_CAPACITY;
        return queue[slot].valid && queue[slot].cmd.tick == nextTick;
    }

    [[nodiscard]] UserCmd popNext() noexcept {
        const uint32_t nextTick = lastProcessedTick + 1;
        const size_t slot = nextTick % QUEUE_CAPACITY;

        UserCmd cmd = queue[slot].cmd;
        queue[slot].valid = false;
        lastProcessedTick = nextTick;
        return cmd;
    }
};

} // namespace irox::net