#pragma once

#include "core/types.hpp"
#include <string>
#include <vector>
#include <array>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include <cstring>
#include <unordered_map>

namespace nemu::core::network {

/// Local wireless play session (Nintendo Switch LDN protocol emulation).
/// Clean-room implementation of a Ryujinx-LDN-style LAN session layer:
/// sessions are announced over UDP broadcast so multiple Nemu instances
/// (or any compatible client) on the same LAN discover each other and sync
/// the LDN Station/Node state needed by games for local wireless lobbies.
struct LdnSessionInfo {
    static constexpr size_t MAX_SESSIONS = 16;
    static constexpr size_t MAX_PLAYERS = 8;

    u16 id{0};                       // random session id
    char name[32]{};                 // lobby name (game controlled)
    u32 game_id{0};                  // title id low bits for filtering
    u8 player_count{0};
    u8 max_players{MAX_PLAYERS};
    u8 host_mac[6]{};                // sender identity
    u16 node_id{0};                  // assigned node ID (0=Host, 1..7=Client)
    u64 created_ms{0};
};

/// Real UDP LAN backend for LDN & Nintendo PIA mesh multiplayer emulation.
class LdnUdpNetwork {
public:
    static constexpr u16 LDN_PORT = 26575;
    static constexpr size_t MAX_FRAGMENT_PAYLOAD = 1300; // MTU safe

    LdnUdpNetwork() = default;
    ~LdnUdpNetwork();

    LdnUdpNetwork(const LdnUdpNetwork&) = delete;
    LdnUdpNetwork& operator=(const LdnUdpNetwork&) = delete;

    bool Initialize();
    void Shutdown();

    /// Broadcast our presence / session announcement onto the LAN.
    bool Broadcast(const LdnSessionInfo& session);

    /// Probe the LAN for sessions; returns sessions seen in the last probe
    /// window (plus this round's newly received announcements).
    std::vector<LdnSessionInfo> Probe();

    /// Direct data exchange between stations (lobby state, in-game packets).
    bool SendTo(const u8* mac, const u8* data, size_t len);

    /// Poll incoming station data (non-blocking). Returns bytes written.
    size_t Receive(u8* out, size_t max_len, u8* from_mac = nullptr);

    /// Dispatch Nintendo PIA mesh packet with automated fragmentation
    bool SendPiaPacket(const u8* dest_mac, u16 node_id, const u8* data, size_t len);

    /// Receive defragmented Nintendo PIA mesh packet
    size_t ReceivePiaPacket(u8* out_data, size_t max_len, u8* from_mac = nullptr, u16* out_node_id = nullptr);

    [[nodiscard]] bool IsOnline() const noexcept { return socket_fd_ >= 0; }
    [[nodiscard]] std::array<u8, 6> LocalMac() const noexcept { return local_mac_; }

    /// Monotonic milliseconds since process start (session expiry clock).
    static u64 NowMs();

private:
    void ConfigureBroadcast();

    struct ReassemblyEntry {
        u32 total_size{0};
        u16 total_fragments{0};
        u16 received_fragments{0};
        std::vector<u8> buffer;
        std::vector<bool> fragment_mask;
        u64 last_updated_ms{0};
    };

    std::atomic<int> socket_fd_{-1};
    std::array<u8, 6> local_mac_{};
    std::mutex rx_mutex_;
    std::vector<LdnSessionInfo> discovered_;
    std::unordered_map<u64, ReassemblyEntry> reassembly_table_;
    std::atomic<u32> pia_sequence_{1};
    bool initialized_{false};
};

/// The LDN service state machine mirrored by the ldn:u IPC service and the
/// NSO screen: station scan -> create/join session -> connected lobby.
class LdnStation {
public:
    enum class State {
        Initialized,
        AccessPointOpened,   // hosting, advertising
        StationConnected,    // joined a session
        Error,
    };

    explicit LdnStation(LdnUdpNetwork& net);

    /// Open an access point (host a lobby) with the given game id.
    bool CreateAccessPoint(const char* name, u32 game_id, u8 max_players);

    /// Close the hosted lobby / leave the joined one.
    bool CloseAccessPoint();

    /// Scan the LAN for lobbies matching a game id (0 = any).
    std::vector<LdnSessionInfo> Scan(u32 game_id);

    /// Join the discovered session by id.
    bool Connect(u16 session_id);

    /// Disconnect from current lobby / access point
    bool Disconnect();

    [[nodiscard]] State GetState() const noexcept { return state_; }
    [[nodiscard]] const LdnSessionInfo& LocalSession() const noexcept { return local_; }
    [[nodiscard]] u8 PlayerCount() const noexcept { return local_.player_count; }
    [[nodiscard]] u16 GetNodeId() const noexcept { return local_.node_id; }

private:
    LdnUdpNetwork& net_;
    LdnSessionInfo local_{};
    State state_{State::Initialized};
};

} // namespace nemu::core::network
