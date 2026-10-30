#pragma once
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <functional>
#include <vector>
#include <span>

namespace irox::net {

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
    void broadcastPacket(const void* data, size_t size, bool reliable = false);

    bool isServer() const { return m_isServer; }
    bool isConnected() const { return m_isConnected; }
    HSteamNetConnection getServerConnection() const { return m_serverConnection; }

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