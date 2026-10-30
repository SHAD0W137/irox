#include "NetworkManager.h"
#include <SDL3/SDL_log.h>
#include <iostream>

namespace irox::net {

NetworkManager* NetworkManager::s_instance = nullptr;

bool NetworkManager::init() {
    s_instance = this;
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        SDL_Log("[GNS] Init failed: %s", errMsg);
        return false;
    }

    m_interface = SteamNetworkingSockets();
    if (!m_interface) return false;

    // Global callback for tracking connection state
    SteamNetworkingUtils()->SetGlobalConfigValuePtr(
        k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
        (void*) onConnectionStatusChanged
    );

    SDL_Log("[GNS] Initialized successfully with global callback.");
    return true;
}

void NetworkManager::shutdown() {
    if (m_interface) {
        if (m_isServer) {
            for (auto conn : m_clientConnections) {
                m_interface->CloseConnection(conn, 0, "Server Shutdown", false);
            }
            m_clientConnections.clear();
            if (m_listenSocket != k_HSteamListenSocket_Invalid) {
                m_interface->CloseListenSocket(m_listenSocket);
            }
            if (m_pollGroup != k_HSteamNetPollGroup_Invalid) {
                m_interface->DestroyPollGroup(m_pollGroup);
            }
        } else {
            if (m_serverConnection != k_HSteamNetConnection_Invalid) {
                m_interface->CloseConnection(m_serverConnection, 0, "Client Disconnect", true);
            }
        }
    }
    GameNetworkingSockets_Kill();
    m_interface = nullptr;
    s_instance = nullptr;
}

bool NetworkManager::startServer(uint16_t port) {
    m_isServer = true;

    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();
    serverAddr.m_port = port;

    m_listenSocket = m_interface->CreateListenSocketIP(serverAddr, 0, nullptr);
    if (m_listenSocket == k_HSteamListenSocket_Invalid) {
        SDL_Log("[GNS Server] Failed to bind to port %u", port);
        return false;
    }

    m_pollGroup = m_interface->CreatePollGroup();
    SDL_Log("[GNS Server] Listening on UDP port %u...", port);
    return true;
}

bool NetworkManager::connectToServer(const char* ipStr, uint16_t port) {
    m_isServer = false;
    m_isConnected = false;

    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();
    if (!serverAddr.ParseString(ipStr)) {
        SDL_Log("[GNS Client] ERROR: Invalid IP address string: '%s'", ipStr);
        return false;
    }
    serverAddr.m_port = port;

    m_serverConnection = m_interface->ConnectByIPAddress(serverAddr, 0, nullptr);
    SDL_Log("[GNS Client] Connecting to %s:%u (handle=%u)...", ipStr, port, m_serverConnection);
    return m_serverConnection != k_HSteamNetConnection_Invalid;
}

void NetworkManager::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    if (!s_instance) return;

    SDL_Log("[GNS] Status changed: state=%d, debug='%s'", info->m_info.m_eState, info->m_info.m_szEndDebug);

    switch (info->m_info.m_eState) {
    case k_ESteamNetworkingConnectionState_Connecting:
        if (s_instance->m_isServer) {
            s_instance->m_interface->AcceptConnection(info->m_hConn);
            s_instance->m_interface->SetConnectionPollGroup(info->m_hConn, s_instance->m_pollGroup);
            SDL_Log("[GNS Server] Handshake initiated with client...");
        }
        break;

    case k_ESteamNetworkingConnectionState_Connected:
        if (s_instance->m_isServer) {
            s_instance->m_clientConnections.push_back(info->m_hConn);
            SDL_Log("[GNS Server] Client connection ESTABLISHED! Total: %zu", s_instance->m_clientConnections.size());

            if (s_instance->onClientConnected) {
                s_instance->onClientConnected(info->m_hConn);
            }
        } else {
            s_instance->m_isConnected = true;
            SDL_Log("[GNS Client] Successfully CONNECTED to server!");
        }
        break;

    case k_ESteamNetworkingConnectionState_ClosedByPeer:
    case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        SDL_Log("[GNS] Connection CLOSED / FAILED: %s", info->m_info.m_szEndDebug);
        if (s_instance->m_isServer) {
            std::erase(s_instance->m_clientConnections, info->m_hConn);
            if (s_instance->onClientDisconnected) {
                s_instance->onClientDisconnected(info->m_hConn);
            }
        } else {
            s_instance->m_isConnected = false;
        }
        s_instance->m_interface->CloseConnection(info->m_hConn, 0, nullptr, false);
        break;

    default:
        break;
    }
}

void NetworkManager::poll(const std::function<void(HSteamNetConnection conn, const uint8_t* data, size_t size)>& onPacketReceived) {
    if (!m_interface) return;

    m_interface->RunCallbacks();

    ISteamNetworkingMessage* messages[32];
    int numMsgs = 0;

    if (m_isServer) {
        numMsgs = m_interface->ReceiveMessagesOnPollGroup(m_pollGroup, messages, 32);
    } else if (m_serverConnection != k_HSteamNetConnection_Invalid) {
        numMsgs = m_interface->ReceiveMessagesOnConnection(m_serverConnection, messages, 32);
    }

    for (int i = 0; i < numMsgs; ++i) {
        auto* msg = messages[i];
        onPacketReceived(msg->m_conn, static_cast<const uint8_t*>(msg->GetData()), msg->GetSize());
        msg->Release();
    }
}

bool NetworkManager::sendPacket(HSteamNetConnection conn, const void* data, size_t size, bool reliable) {
    if (!m_interface) return false;
    int sendFlags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay;
    return m_interface->SendMessageToConnection(conn, data, static_cast<uint32_t>(size), sendFlags, nullptr) == k_EResultOK;
}

void NetworkManager::broadcastPacket(const void* data, size_t size, bool reliable) {
    for (auto conn : m_clientConnections) {
        sendPacket(conn, data, size, reliable);
    }
}

NetworkManager::~NetworkManager() {
    shutdown();
}

} // namespace irox::net