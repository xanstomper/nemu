#include "ldn_network.hpp"
#include "platform/logger.hpp"

#include <cstdint>
#include <random>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
using send_size_t = int;
using recv_size_t = int;
#define NEMU_INVALID_SOCKET INVALID_SOCKET
#define NEMU_SOCKET_ERROR SOCKET_ERROR
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
using socket_t = int;
using send_size_t = size_t;
using recv_size_t = size_t;
#define NEMU_INVALID_SOCKET (-1)
#define NEMU_SOCKET_ERROR (-1)
#endif

namespace nemu::core::network {

namespace {

constexpr u8 LDN_MAGIC[4] = {'N', 'L', 'D', 'N'};

enum PacketType : u8 {
    PKT_ANNOUNCE     = 1,   // session advertisement (host -> LAN broadcast)
    PKT_PROBE        = 2,   // discovery request (client -> LAN broadcast)
    PKT_STATION      = 3,   // direct station-to-station payload
    PKT_CONNECT_REQ  = 4,   // join request (client -> host)
    PKT_CONNECT_ACK  = 5,   // join response (host -> client)
    PKT_DISCONNECT   = 6,   // station leave notification
    PKT_PIA_DATA     = 7,   // unfragmented Nintendo PIA packet
    PKT_PIA_FRAGMENT = 8,   // fragmented Nintendo PIA packet
};

#pragma pack(push, 1)
struct LdnPacketHeader {
    u8 magic[4];
    u8 type;
    u8 node_id;
    u16 payload_size;
    LdnSessionInfo session;
};

struct PiaFragmentHeader {
    u32 sequence_number;
    u16 fragment_index;
    u16 fragment_count;
    u32 total_size;
};
#pragma pack(pop)

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
    pkt.node_id = static_cast<u8>(session.node_id);
    pkt.payload_size = sizeof(LdnSessionInfo);
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
        pkt.node_id = 0;
        pkt.payload_size = 0;
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
        if (n < static_cast<decltype(n)>(sizeof(LdnPacketHeader))) break;
        if (std::memcmp(pkt.magic, LDN_MAGIC, 4) != 0) continue;
        if (pkt.type != PKT_ANNOUNCE) continue;
        if (std::memcmp(pkt.session.host_mac, local_mac_.data(), 6) == 0) continue; // self

        // Merge into discovered list
        bool found = false;
        for (auto& s : discovered_) {
            if (s.id == pkt.session.id && std::memcmp(s.host_mac, pkt.session.host_mac, 6) == 0) {
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
    auto sent = ::sendto(fd, reinterpret_cast<const char*>(data), static_cast<send_size_t>(len), 0,
                         reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    return sent != NEMU_SOCKET_ERROR;
}

size_t LdnUdpNetwork::Receive(u8* out, size_t max_len, u8* from_mac) {
    if (!IsOnline()) return 0;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    auto n = ::recvfrom(fd, reinterpret_cast<char*>(out), static_cast<recv_size_t>(max_len), 0,
                        reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) return 0;
    if (from_mac) {
        from_mac[0] = 0x02; from_mac[1] = 0x00;
        std::memcpy(from_mac + 2, &from.sin_addr.s_addr, 4);
    }
    return static_cast<size_t>(n);
}

bool LdnUdpNetwork::SendPiaPacket(const u8* dest_mac, u16 node_id, const u8* data, size_t len) {
    if (!IsOnline()) return false;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(LDN_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    if (len <= MAX_FRAGMENT_PAYLOAD) {
        std::vector<u8> buffer(sizeof(LdnPacketHeader) + len);
        LdnPacketHeader* hdr = reinterpret_cast<LdnPacketHeader*>(buffer.data());
        std::memcpy(hdr->magic, LDN_MAGIC, 4);
        hdr->type = PKT_PIA_DATA;
        hdr->node_id = static_cast<u8>(node_id);
        hdr->payload_size = static_cast<u16>(len);
        std::copy_n(local_mac_.data(), 6, hdr->session.host_mac);
        if (dest_mac) {
            std::copy_n(dest_mac, 6, hdr->session.host_mac);
        }
        std::memcpy(buffer.data() + sizeof(LdnPacketHeader), data, len);

        auto sent = ::sendto(fd, reinterpret_cast<const char*>(buffer.data()), static_cast<send_size_t>(buffer.size()), 0,
                             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        return sent != NEMU_SOCKET_ERROR;
    }

    // Multi-packet fragmentation
    const u32 seq = pia_sequence_.fetch_add(1, std::memory_order_relaxed);
    const u16 total_fragments = static_cast<u16>((len + MAX_FRAGMENT_PAYLOAD - 1) / MAX_FRAGMENT_PAYLOAD);

    for (u16 idx = 0; idx < total_fragments; ++idx) {
        const size_t offset = static_cast<size_t>(idx) * MAX_FRAGMENT_PAYLOAD;
        const size_t chunk_size = std::min(MAX_FRAGMENT_PAYLOAD, len - offset);

        const size_t pkt_size = sizeof(LdnPacketHeader) + sizeof(PiaFragmentHeader) + chunk_size;
        std::vector<u8> frag_buf(pkt_size);

        LdnPacketHeader* hdr = reinterpret_cast<LdnPacketHeader*>(frag_buf.data());
        std::memcpy(hdr->magic, LDN_MAGIC, 4);
        hdr->type = PKT_PIA_FRAGMENT;
        hdr->node_id = static_cast<u8>(node_id);
        hdr->payload_size = static_cast<u16>(sizeof(PiaFragmentHeader) + chunk_size);
        std::copy_n(local_mac_.data(), 6, hdr->session.host_mac);

        PiaFragmentHeader* fhdr = reinterpret_cast<PiaFragmentHeader*>(frag_buf.data() + sizeof(LdnPacketHeader));
        fhdr->sequence_number = seq;
        fhdr->fragment_index = idx;
        fhdr->fragment_count = total_fragments;
        fhdr->total_size = static_cast<u32>(len);

        std::memcpy(frag_buf.data() + sizeof(LdnPacketHeader) + sizeof(PiaFragmentHeader), data + offset, chunk_size);

        ::sendto(fd, reinterpret_cast<const char*>(frag_buf.data()), static_cast<send_size_t>(frag_buf.size()), 0,
                 reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    }

    return true;
}

size_t LdnUdpNetwork::ReceivePiaPacket(u8* out_data, size_t max_len, u8* from_mac, u16* out_node_id) {
    if (!IsOnline()) return 0;
    socket_t fd = static_cast<socket_t>(socket_fd_.load());

    std::lock_guard<std::mutex> lk(rx_mutex_);
    const u64 now = NowMs();

    // Expire old fragments (> 3000ms)
    for (auto it = reassembly_table_.begin(); it != reassembly_table_.end();) {
        if (now - it->second.last_updated_ms > 3000) {
            it = reassembly_table_.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<u8> raw_buf(sizeof(LdnPacketHeader) + sizeof(PiaFragmentHeader) + MAX_FRAGMENT_PAYLOAD + 128);
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    auto n = ::recvfrom(fd, reinterpret_cast<char*>(raw_buf.data()), static_cast<recv_size_t>(raw_buf.size()), 0,
                        reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < static_cast<decltype(n)>(sizeof(LdnPacketHeader))) return 0;

    const LdnPacketHeader* hdr = reinterpret_cast<const LdnPacketHeader*>(raw_buf.data());
    if (std::memcmp(hdr->magic, LDN_MAGIC, 4) != 0) return 0;

    if (from_mac) {
        std::memcpy(from_mac, hdr->session.host_mac, 6);
    }
    if (out_node_id) {
        *out_node_id = hdr->node_id;
    }

    // Direct unfragmented PIA packet
    if (hdr->type == PKT_PIA_DATA) {
        const size_t payload_len = std::min(static_cast<size_t>(hdr->payload_size), max_len);
        std::memcpy(out_data, raw_buf.data() + sizeof(LdnPacketHeader), payload_len);
        return payload_len;
    }

    // Fragmented PIA packet
    if (hdr->type == PKT_PIA_FRAGMENT) {
        if (static_cast<size_t>(n) < sizeof(LdnPacketHeader) + sizeof(PiaFragmentHeader)) {
            return 0;
        }

        const PiaFragmentHeader* fhdr = reinterpret_cast<const PiaFragmentHeader*>(raw_buf.data() + sizeof(LdnPacketHeader));
        const u64 key = (static_cast<u64>(fhdr->sequence_number) << 32) | (static_cast<u64>(hdr->session.host_mac[0]) << 16) | hdr->session.host_mac[1];

        auto& entry = reassembly_table_[key];
        if (entry.total_fragments == 0) {
            entry.total_size = fhdr->total_size;
            entry.total_fragments = fhdr->fragment_count;
            entry.received_fragments = 0;
            entry.buffer.resize(fhdr->total_size);
            entry.fragment_mask.resize(fhdr->fragment_count, false);
            entry.last_updated_ms = now;
        }

        if (fhdr->fragment_index < entry.total_fragments && !entry.fragment_mask[fhdr->fragment_index]) {
            entry.fragment_mask[fhdr->fragment_index] = true;
            entry.received_fragments++;
            entry.last_updated_ms = now;

            const size_t offset = static_cast<size_t>(fhdr->fragment_index) * MAX_FRAGMENT_PAYLOAD;
            const size_t chunk_size = static_cast<size_t>(hdr->payload_size) - sizeof(PiaFragmentHeader);
            const size_t copy_size = std::min(chunk_size, entry.buffer.size() - offset);

            std::memcpy(entry.buffer.data() + offset,
                        raw_buf.data() + sizeof(LdnPacketHeader) + sizeof(PiaFragmentHeader),
                        copy_size);
        }

        if (entry.received_fragments >= entry.total_fragments) {
            const size_t complete_size = std::min(static_cast<size_t>(entry.total_size), max_len);
            std::memcpy(out_data, entry.buffer.data(), complete_size);
            reassembly_table_.erase(key);
            return complete_size;
        }
    }

    return 0;
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
    local_.max_players = max_players > 0 ? max_players : 8;
    local_.node_id = 0; // Host is node 0
    auto mac = net_.LocalMac();
    std::copy_n(mac.data(), 6, local_.host_mac);
    local_.created_ms = LdnUdpNetwork::NowMs();

    state_ = State::AccessPointOpened;
    net_.Broadcast(local_);
    NEMU_LOG_INFO("LDN", "Access point '{}' opened (session id 0x{:04X}, game 0x{:08X})",
                  local_.name, local_.id, local_.game_id);
    return true;
}

bool LdnStation::CloseAccessPoint() {
    if (state_ != State::AccessPointOpened && state_ != State::StationConnected) return false;
    Disconnect();
    return true;
}

std::vector<LdnSessionInfo> LdnStation::Scan(u32 game_id) {
    auto all = net_.Probe();
    if (game_id == 0) return all;
    std::vector<LdnSessionInfo> filtered;
    for (const auto& s : all) {
        if (s.game_id == game_id) filtered.push_back(s);
    }
    return filtered;
}

bool LdnStation::Connect(u16 session_id) {
    auto sessions = net_.Probe();
    for (const auto& s : sessions) {
        if (s.id == session_id) {
            local_ = s;
            local_.player_count++;
            local_.node_id = local_.player_count - 1; // Assign next node id
            state_ = State::StationConnected;

            // Notify host of connect
            LdnPacketHeader pkt{};
            std::memcpy(pkt.magic, LDN_MAGIC, 4);
            pkt.type = PKT_CONNECT_REQ;
            pkt.node_id = static_cast<u8>(local_.node_id);
            pkt.session = local_;
            auto mac = net_.LocalMac();
            std::copy_n(mac.data(), 6, pkt.session.host_mac);
            net_.SendTo(s.host_mac, reinterpret_cast<const u8*>(&pkt), sizeof(pkt));

            NEMU_LOG_INFO("LDN", "Connected to session 0x{:04X} '{}' as node {}",
                          session_id, s.name, local_.node_id);
            return true;
        }
    }
    return false;
}

bool LdnStation::Disconnect() {
    if (state_ == State::Initialized) return true;

    LdnPacketHeader pkt{};
    std::memcpy(pkt.magic, LDN_MAGIC, 4);
    pkt.type = PKT_DISCONNECT;
    pkt.node_id = static_cast<u8>(local_.node_id);
    pkt.session = local_;
    auto mac = net_.LocalMac();
    std::copy_n(mac.data(), 6, pkt.session.host_mac);
    net_.SendTo(local_.host_mac, reinterpret_cast<const u8*>(&pkt), sizeof(pkt));

    local_ = {};
    state_ = State::Initialized;
    NEMU_LOG_INFO("LDN", "Disconnected from LDN session");
    return true;
}

} // namespace nemu::core::network
