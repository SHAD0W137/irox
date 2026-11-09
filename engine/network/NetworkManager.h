#pragma once
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <functional>
#include <vector>
#include "engine/network/NetworkPackets.h"

namespace irox::net {

struct NetworkStats {
    int pingMs = 0;
    float qualityLocal = 1.0f;        // 1.0 = perfect, 0.0 = total loss
    float qualityRemote = 1.0f;       // 1.0 = perfect, 0.0 = total loss
    float inPacketLossPercent = 0.0f;  // Calculated percentage
    float outPacketLossPercent = 0.0f; // Calculated percentage
};

class NetworkManager {
public:
    NetworkManager() = default;
    ~NetworkManager();

    bool init();
    void shutdown();

    bool startServer(uint16_t port);
    bool connectToServer(const char* serverAddress, uint16_t port);
    void poll(const std::function<void(HSteamNetConnection conn, const uint8_t* data, size_t size)>& onPacketReceived);

    bool sendPacket(HSteamNetConnection conn, const void* data, size_t size, bool reliable = false);
    void broadcastSnapshot(const WorldSnapshotPacket& packet);

    // Queries live network quality from Valve GNS
    NetworkStats getConnectionStats(HSteamNetConnection conn);

    bool isServer() const { return m_isServer; }
    bool isConnected() const { return m_isConnected; }
    HSteamNetConnection getServerConnection() const { return m_serverConnection; }
    const std::vector<HSteamNetConnection>& getClientConnections() const { return m_clientConnections; }

    std::function<void(HSteamNetConnection)> onClientConnected;
    std::function<void(HSteamNetConnection)> onClientDisconnected;

private:
    ISteamNetworkingSockets* m_interface = nullptr;
    HSteamListenSocket m_listenSocket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup m_pollGroup = k_HSteamNetPollGroup_Invalid;
    HSteamNetConnection m_serverConnection = k_HSteamNetConnection_Invalid;

    bool m_isServer = false;
    bool m_isConnected = false;
    std::vector<HSteamNetConnection> m_clientConnections;

    static void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    static NetworkManager* s_instance;
};

} // namespace irox::net