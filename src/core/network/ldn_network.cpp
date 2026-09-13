#include "ldn_network.hpp"
#include "platform/logger.hpp"

#include <cstdint>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define NEMU_INVALID_SOCKET INVALID_SOCKET
#define NEMU_SOCKET_ERROR SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
using socket_t = int;
#define NEMU_INVALID_SOCKET (-1)
#define NEMU_SOCKET_ERROR (-1)
#endif

namespace nemu::core::network {

namespace {

constexpr u8 LDN_MAGIC[4] = {'N', 'L', 'D', 'N'};

enum PacketType : u8 {
    PKT_ANNOUNCE = 1,   // session advertisement (host -> LAN broadcast)
    PKT_PROBE = 2,      // discovery request (client -> LAN broadcast)
    PKT_STATION = 3,    // direct station-to-station payload
};

struct [[gnu::packed]] LdnPacketHeader {
    u8 magic[4];
    u8 type;
    u8 pad[3];
    LdnSessionInfo session;
};

void CloseSocket(socket_t fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

} // namespace

LdnUdpNetwork::~LdnUdpNetwork() {
    Shutdown();
}

u64 LdnUdpNetwork::NowMs() {
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

bool LdnUdpNetwork::Initialize() {
    if (initialized_) return true;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        NEMU_LOG_ERROR("LDN", "WSAStartup failed");
        return false;
    }
#endif

    socket_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == NEMU_INVALID_SOCKET) {
        NEMU_LOG_ERROR("LDN", "Failed to create UDP socket");
        return false;
    }

    // Allow broadcast
    int broadcast = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcast), sizeof(broadcast)) < 0) {
        NEMU_LOG_WARN("LDN", "SO_BROADCAST failed; LAN discovery disabled");
    }

    // Reuse address so multiple instances on one machine can coexist
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Non-blocking
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(fd, FIONBIO, &nb);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LDN_PORT);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        // Another Nemu instance owns the port on this machine; use an
        // ephemeral port and rely on broadcast for discovery.
        addr.sin_port = 0;
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            NEMU_LOG_ERROR("LDN", "UDP bind failed");
            CloseSocket(fd);
            return false;
        }
        NEMU_LOG_INFO("LDN", "Bound to ephemeral port (another instance owns {})", LDN_PORT);
    }

    socket_fd_.store(static_cast<int>(fd == NEMU_INVALID_SOCKET ? -1 : fd));

    // Random local MAC identity for the station
    std::random_device rd;
    std::uniform_int_distribution<int> byte(1, 250);
    local_mac_ = {static_cast<u8>(byte(rd)), static_cast<u8>(byte(rd)), static_cast<u8>(byte(rd)),
                  static_cast<u8>(byte(rd)), static_cast<u8>(byte(rd)), static_cast<u8>(byte(rd))};

    initialized_ = true;
    NEMU_LOG_INFO("LDN", "UDP LAN backend ready (mac {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X})",
                  local_mac_[0], local_mac_[1], local_mac_[2], local_mac_[3], local_mac_[4], local_mac_[5]);
    return true;
}

void LdnUdpNetwork::Shutdown() {
    int expected = socket_fd_.load();
    if (expected < 0) return;
    if (socket_fd_.compare_exchange_strong(expected, -1)) {
        CloseSocket(static_cast<socket_t>(expected));
    }
    initialized_ = false;
}

void LdnUdpNetwork::ConfigureBroadcast() {}

bool LdnUdpNetwork::Broadcast(const LdnSessionInfo& session) {
    if (!IsOnline()) return false;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());

    LdnPacketHeader pkt{};
    std::memcpy(pkt.magic, LDN_MAGIC, 4);
    pkt.type = PKT_ANNOUNCE;
    pkt.session = session;

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(LDN_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    auto sent = ::sendto(fd, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
                         reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    return sent != NEMU_SOCKET_ERROR;
}

std::vector<LdnSessionInfo> LdnUdpNetwork::Probe() {
    std::vector<LdnSessionInfo> out;
    if (!IsOnline()) return out;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());

    // Broadcast a probe request
    {
        LdnPacketHeader pkt{};
        std::memcpy(pkt.magic, LDN_MAGIC, 4);
        pkt.type = PKT_PROBE;
        pkt.session = {};
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(LDN_PORT);
        dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        ::sendto(fd, reinterpret_cast<const char*>(&pkt), sizeof(pkt), 0,
                 reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    }

    // Drain all pending announcements (non-blocking)
    std::lock_guard<std::mutex> lk(rx_mutex_);
    u64 now = NowMs();
    for (int i = 0; i < 32; ++i) {
        LdnPacketHeader pkt{};
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        auto n = ::recvfrom(fd, reinterpret_cast<char*>(&pkt), sizeof(pkt), 0,
                            reinterpret_cast<sockaddr*>(&from), &from_len);
        if (n < static_cast< decltype(n) >(sizeof(LdnPacketHeader))) break;
        if (std::memcmp(pkt.magic, LDN_MAGIC, 4) != 0) continue;
        if (pkt.type != PKT_ANNOUNCE) continue;
        if (std::memcmp(pkt.session.host_mac.data(), local_mac_.data(), 6) == 0) continue; // self

        // Merge into discovered list
        bool found = false;
        for (auto& s : discovered_) {
            if (s.id == pkt.session.id && std::memcmp(s.host_mac.data(), pkt.session.host_mac.data(), 6) == 0) {
                s = pkt.session;
                s.created_ms = now;
                found = true;
                break;
            }
        }
        if (!found && discovered_.size() < LdnSessionInfo::MAX_SESSIONS) {
            pkt.session.created_ms = now;
            discovered_.push_back(pkt.session);
        }
    }

    // Expire stale entries (no announcement for 5s) and copy out
    std::vector<LdnSessionInfo> fresh;
    for (auto& s : discovered_) {
        if (now - s.created_ms < 5000) fresh.push_back(s);
    }
    discovered_ = fresh;
    out = discovered_;
    return out;
}

bool LdnUdpNetwork::SendTo(const u8* mac, const u8* data, size_t len) {
    (void)mac;
    if (!IsOnline()) return false;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(LDN_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    auto sent = ::sendto(fd, reinterpret_cast<const char*>(data), len, 0,
                         reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    return sent != NEMU_SOCKET_ERROR;
}

size_t LdnUdpNetwork::Receive(u8* out, size_t max_len, u8* from_mac) {
    if (!IsOnline()) return 0;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    auto n = ::recvfrom(fd, reinterpret_cast<char*>(out), static_cast<int>(max_len), 0,
                        reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) return 0;
    if (from_mac) {
        // Use IPv4 address bytes as station identity for direct data
        from_mac[0] = 0x02; from_mac[1] = 0x00;
        std::memcpy(from_mac + 2, &from.sin_addr.s_addr, 4);
    }
    return static_cast<size_t>(n);
}

LdnStation::LdnStation(LdnUdpNetwork& n) : net_(n) {}

bool LdnStation::CreateAccessPoint(const char* name, u32 game_id, u8 max_players) {
    if (!net_.IsOnline() && !net_.Initialize()) return false;

    std::random_device rd;
    local_ = {};
    local_.id = static_cast<u16>(rd() & 0x7FFF);
    std::snprintf(local_.name, sizeof(local_.name), "%s", name ? name : "Nemu Lobby");
    local_.game_id = game_id;
    local_.player_count = 1;
    local_.max_players = max_players ? max_players : LdnSessionInfo::MAX_PLAYERS;
    local_.host_mac = net_.LocalMac();
    local_.created_ms = LdnUdpNetwork::NowMs();

    state_ = State::AccessPointOpened;
    net_.Broadcast(local_);
    NEMU_LOG_INFO("LDN", "Access point opened: '{}' (game={:08X}, id={})", local_.name, game_id, local_.id);
    return true;
}

bool LdnStation::CloseAccessPoint() {
    state_ = State::Initialized;
    local_ = {};
    NEMU_LOG_INFO("LDN", "Access point closed");
    return true;
}

std::vector<LdnSessionInfo> LdnStation::Scan(u32 game_id) {
    auto sessions = net_.Probe();
    if (game_id == 0) return sessions;
    std::vector<LdnSessionInfo> filtered;
    for (auto& s : sessions) {
        if (s.game_id == game_id) filtered.push_back(s);
    }
    return filtered;
}

bool LdnStation::Connect(u16 session_id) {
    auto sessions = net_.Probe();
    for (const auto& s : sessions) {
        if (s.id == session_id) {
            local_ = s;
            state_ = State::StationConnected;
            NEMU_LOG_INFO("LDN", "Connected to session '{}' (id={}, host {:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X})",
                          s.name, s.id, s.host_mac[0], s.host_mac[1], s.host_mac[2], s.host_mac[3], s.host_mac[4], s.host_mac[5]);
            return true;
        }
    }
    return false;
}

} // namespace nemu::core::network
