// ---------------------------------------------------------------------------
// GDB Remote Serial Protocol (RSP) TCP stub — implementation
// ---------------------------------------------------------------------------

#include "core/debug/gdb_stub.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <format>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  ifdef _MSC_VER
#    pragma comment(lib, "ws2_32.lib") // MSVC-only; MinGW links ws2_32 via CMake
#  endif
#else
#  include <fcntl.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#endif

namespace nemu::core::debug {

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

static void CloseSocket(SocketFd fd) {
    if (fd == kInvalidSocket) return;
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

// ---------------------------------------------------------------------------
// GdbStub lifecycle
// ---------------------------------------------------------------------------

GdbStub::~GdbStub() {
    Stop();
}

bool GdbStub::Start(u16 port) {
    if (running_.load(std::memory_order_relaxed)) return true;

#ifdef _WIN32
    if (!wsa_initialized_) {
        WSADATA wsa_data{};
        if (::WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
        wsa_initialized_ = true;
    }
#endif

    // Create listen socket.
    SocketFd fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) return false;

    // SO_REUSEADDR so we can restart quickly.
    {
        int opt = 1;
#ifdef _WIN32
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket(fd);
        return false;
    }
    if (::listen(fd, 1) != 0) {
        CloseSocket(fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(listen_fd_mutex_);
        listen_fd_ = fd;
    }

    running_.store(true, std::memory_order_relaxed);
    server_thread_ = std::thread(&GdbStub::ServerThread, this);
    return true;
}

void GdbStub::Stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    // Close listen socket to unblock accept().
    {
        std::lock_guard<std::mutex> lock(listen_fd_mutex_);
        if (listen_fd_ != kInvalidSocket) {
            CloseSocket(listen_fd_);
            listen_fd_ = kInvalidSocket;
        }
    }

    if (server_thread_.joinable()) server_thread_.join();

#ifdef _WIN32
    if (wsa_initialized_) {
        ::WSACleanup();
        wsa_initialized_ = false;
    }
#endif
}

// ---------------------------------------------------------------------------
// Server thread: accept one client at a time
// ---------------------------------------------------------------------------

void GdbStub::ServerThread() {
    while (running_.load(std::memory_order_relaxed)) {
        SocketFd lfd;
        {
            std::lock_guard<std::mutex> lock(listen_fd_mutex_);
            lfd = listen_fd_;
        }
        if (lfd == kInvalidSocket) break;

        // Wait for an incoming connection with a bounded poll so Stop() can
        // break a blocked accept() (closing the fd from another thread does NOT
        // reliably interrupt a blocking accept() on Linux/Win). Re-check the
        // running_ flag every poll interval.
        bool ready = false;
#ifdef _WIN32
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200 * 1000; // 200 ms
        const int s = ::select(static_cast<int>(lfd) + 1, &rfds, nullptr, nullptr, &tv);
        ready = (s > 0 && FD_ISSET(lfd, &rfds));
#else
        pollfd pfd{};
        pfd.fd = lfd;
        pfd.events = POLLIN;
        const int s = ::poll(&pfd, 1, 200); // 200 ms
        ready = (s > 0 && (pfd.revents & POLLIN));
#endif
        if (!ready) continue; // loop: re-checks running_

        sockaddr_in client_addr{};
#ifdef _WIN32
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        SocketFd client = ::accept(lfd, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
        if (client == kInvalidSocket) continue; // transient; loop re-checks running_

        HandleClient(client);
        CloseSocket(client);
    }

    running_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Per-client handler loop
// ---------------------------------------------------------------------------

void GdbStub::HandleClient(SocketFd client_fd) {
    // Disable Nagle for responsive packet exchange.
    {
        int flag = 1;
#ifdef _WIN32
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&flag), sizeof(flag));
#else
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
#endif
    }

    while (running_.load(std::memory_order_relaxed)) {
        std::string pkt = RecvPacket(client_fd);
        if (pkt.empty()) break; // EOF or error

        // Detach packet: reply OK then close
        if (!pkt.empty() && pkt[0] == 'D') {
            SendPacket(client_fd, "OK");
            break;
        }

        std::string reply = DispatchPacket(pkt);
        if (!SendPacket(client_fd, reply)) break;
    }
}

// ---------------------------------------------------------------------------
// RSP low-level I/O
// ---------------------------------------------------------------------------

bool GdbStub::RecvByte(SocketFd fd, char& out) {
#ifdef _WIN32
    int n = ::recv(fd, &out, 1, 0);
#else
    ssize_t n = ::recv(fd, &out, 1, 0);
#endif
    return n == 1;
}

bool GdbStub::SendRaw(SocketFd fd, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = ::send(fd, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR) return false;
#else
        ssize_t n = ::send(fd, data + sent, static_cast<size_t>(len - sent), 0);
        if (n <= 0) return false;
#endif
        sent += static_cast<int>(n);
    }
    return true;
}

bool GdbStub::SendRaw(SocketFd fd, const std::string& s) {
    return SendRaw(fd, s.data(), static_cast<int>(s.size()));
}

// Read one RSP packet: $payload#cc
// Skips leading '+'/'-' (ACK/NAK bytes from the client).
// Returns the payload string, or empty on disconnect/error.
std::string GdbStub::RecvPacket(SocketFd fd) {
    char ch;
    // Skip ACK/NAK bytes until '$'
    while (true) {
        if (!RecvByte(fd, ch)) return {};
        if (ch == '$') break;
        // '+' / '-' are ACKs – consume silently.
    }

    // Read until '#'
    std::string payload;
    payload.reserve(128);
    while (true) {
        if (!RecvByte(fd, ch)) return {};
        if (ch == '#') break;
        // Handle escape byte '}': next byte XOR 0x20
        if (ch == '}') {
            char esc;
            if (!RecvByte(fd, esc)) return {};
            payload += static_cast<char>(esc ^ 0x20);
        } else {
            payload += ch;
        }
    }

    // Read 2-byte checksum
    char cs[2];
    if (!RecvByte(fd, cs[0]) || !RecvByte(fd, cs[1])) return {};

    // Verify checksum
    u8 expected = 0;
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
        for (size_t i = 0; i < payload.size(); ++i) {
            expected = static_cast<u8>(expected + bytes[i]);
        }
    }

    char expected_str[3];
    std::snprintf(expected_str, sizeof(expected_str), "%02x",
                  static_cast<unsigned>(expected));

    if (cs[0] == expected_str[0] && cs[1] == expected_str[1]) {
        // Good packet – send ACK
        SendRaw(fd, "+", 1);
    } else {
        // Bad checksum – send NAK
        SendRaw(fd, "-", 1);
        // Return empty to retry (caller will try again in the loop)
        return {};
    }

    return payload;
}

// Encode payload into $payload#cc and transmit (preceded by '+' ACK so that
// GDB at the other end knows the previous packet was accepted).
bool GdbStub::SendPacket(SocketFd fd, const std::string& payload) {
    // Compute checksum
    u8 cksum = 0;
    {
        const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
        for (size_t i = 0; i < payload.size(); ++i) {
            cksum = static_cast<u8>(cksum + bytes[i]);
        }
    }

    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x", static_cast<unsigned>(cksum));

    std::string frame;
    frame.reserve(payload.size() + 4);
    frame += '+';    // ACK for the packet we just received
    frame += '$';
    frame += payload;
    frame += buf;    // #cc

    return SendRaw(fd, frame);
}

// ---------------------------------------------------------------------------
// Utility: byte buffer → lower-case hex string
// ---------------------------------------------------------------------------
static std::string BytesToHex(const u8* data, size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += kHex[(data[i] >> 4) & 0xF];
        out += kHex[data[i] & 0xF];
    }
    return out;
}

// 8-byte value as little-endian hex (GDB AArch64 register byte order).
static std::string U64ToLeHex(u64 v) {
    u8 le[8];
    for (size_t b = 0; b < 8; ++b) le[b] = static_cast<u8>(v >> (b * 8));
    return BytesToHex(le, 8);
}

// Hex string → byte vector (up to `max_bytes`)
static std::vector<u8> HexToBytes(const std::string& hex) {
    std::vector<u8> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto nibble = [](char c) -> u8 {
            if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
            return 0;
        };
        out.push_back(static_cast<u8>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Packet dispatcher
// ---------------------------------------------------------------------------

std::string GdbStub::DispatchPacket(const std::string& pkt) {
    if (pkt.empty()) return "";

    switch (pkt[0]) {
        case '?':
            return HandleHaltReason();

        case 'g':
            return HandleReadRegisters();

        case 'G':
            return HandleWriteRegisters(pkt.substr(1));

        case 'p':
            return HandleReadRegister(pkt.substr(1));

        case 'P':
            return HandleWriteRegister(pkt.substr(1));

        case 'm':
            return HandleReadMemory(pkt.substr(1));

        case 'M':
            return HandleWriteMemory(pkt.substr(1));

        case 'c':
        case 's':
            // Continue / single-step: report SIGTRAP
            return "S05";

        case 'H':
            // Set thread – always succeed
            return "OK";

        case 'D':
            // Detach – handled in HandleClient before we get here, but just in case
            return "OK";

        case 'q':
            return HandleQuery(pkt.substr(1));

        default:
            return "";
    }
}

// ---------------------------------------------------------------------------
// Individual packet handlers
// ---------------------------------------------------------------------------

std::string GdbStub::HandleHaltReason() {
    // T05 = SIGTRAP (breakpoint / initial stop)
    return "T05";
}

cpu::CpuState* GdbStub::GetLiveCpuState() noexcept {
    std::lock_guard<std::mutex> lock(state_provider_mutex_);
    if (!state_provider_) return nullptr;
    return state_provider_();
}

u64 GdbStub::ReadGdbReg(u32 idx) const noexcept {
    // The accessor needs a non-const CpuState*; live reads are inherently
    // best-effort (the guest runs concurrently), so we take a snapshot via the
    // live pointer the provider hands us. idx: 0-30 => x0-x30, 31 => pc, 32 => sp.
    if (idx >= kNumGdbRegs) return 0;
    // const_cast: the provider returns a live mutable state; we only read it.
    std::lock_guard<std::mutex> lock(const_cast<GdbStub*>(this)->state_provider_mutex_);
    if (!state_provider_) return 0;
    cpu::CpuState* st = state_provider_();
    if (!st) return 0;
    if (idx < 31) return st->GetX(idx); // 0..30
    if (idx == 31) return st->pc;
    if (idx == 32) return st->sp;
    return 0;
}

void GdbStub::WriteGdbReg(u32 idx, u64 val) noexcept {
    if (idx >= kNumGdbRegs) return;
    cpu::CpuState* st = GetLiveCpuState();
    if (!st) return;
    if (idx < 31) {
        st->SetX(idx, val);
    } else if (idx == 31) {
        st->pc = val;
    } else if (idx == 32) {
        st->sp = val;
    }
}

std::string GdbStub::HandleReadRegisters() {
    // g → read all registers. GDB AArch64 expects x0..x30 then pc then sp,
    // each 8 bytes little-endian hex. With live state wired we report the real
    // guest registers; otherwise zeros (as before).
    std::string result;
    result.reserve(kNumGdbRegs * 16);
    for (u32 i = 0; i < kNumGdbRegs; ++i) {
        result += U64ToLeHex(ReadGdbReg(i));
    }
    return result;
}

std::string GdbStub::HandleWriteRegisters(const std::string& data) {
    // G…<bytes> — write all registers. Only honor it when we have live state;
    // otherwise silently OK (no-op) so GDB doesn't error out.
    if (data.size() >= kNumGdbRegs * 16) {
        for (u32 i = 0; i < kNumGdbRegs; ++i) {
            const std::string hex = data.substr(i * 16, 16);
            u64 v = 0;
            for (size_t b = 0; b < 8; ++b) {
                const auto nib = [](char c) -> u8 {
                    if (c >= '0' && c <= '9') return static_cast<u8>(c - '0');
                    if (c >= 'a' && c <= 'f') return static_cast<u8>(c - 'a' + 10);
                    if (c >= 'A' && c <= 'F') return static_cast<u8>(c - 'A' + 10);
                    return 0;
                };
                v |= static_cast<u64>(nib(hex[b * 2]) | (nib(hex[b * 2 + 1]) << 4)) << (b * 8);
            }
            WriteGdbReg(i, v);
        }
    }
    return "OK";
}

std::string GdbStub::HandleReadRegister(const std::string& args) {
    // p N — read a single register. N is hex. Return 16 hex digits, or E01.
    u32 idx = 0;
    try {
        idx = static_cast<u32>(std::stoul(args, nullptr, 16));
    } catch (...) {
        return "E01";
    }
    if (idx >= kNumGdbRegs) return "E01";
    return U64ToLeHex(ReadGdbReg(idx));
}

std::string GdbStub::HandleWriteRegister(const std::string& args) {
    // P N=value — write a single register. N and value are hex.
    const auto eq = args.find('=');
    if (eq == std::string::npos) return "E01";
    u32 idx = 0;
    u64 val = 0;
    try {
        idx = static_cast<u32>(std::stoul(args.substr(0, eq), nullptr, 16));
        val = static_cast<u64>(std::stoull(args.substr(eq + 1), nullptr, 16));
    } catch (...) {
        return "E01";
    }
    WriteGdbReg(idx, val);
    return "OK";
}

std::string GdbStub::HandleReadMemory(const std::string& args) {
    // args = "addr,len"  (hex values)
    const auto comma = args.find(',');
    if (comma == std::string::npos) return "E01";

    vaddr_t addr = 0;
    size_t  len  = 0;
    try {
        addr = static_cast<vaddr_t>(std::stoull(args.substr(0, comma), nullptr, 16));
        len  = static_cast<size_t> (std::stoull(args.substr(comma + 1), nullptr, 16));
    } catch (...) {
        return "E01";
    }

    if (len == 0) return "";
    // Cap to a reasonable maximum to avoid OOM
    constexpr size_t kMaxReadLen = 0x4000;
    if (len > kMaxReadLen) len = kMaxReadLen;

    std::vector<u8> buf(len, 0u);

    if (memory_ != nullptr) {
        memory_->ReadBlock(addr, buf.data(), len);
    }
    // If no memory_ or ReadBlock fails, buf stays zero-filled.

    return BytesToHex(buf.data(), buf.size());
}

std::string GdbStub::HandleWriteMemory(const std::string& args) {
    // args = "addr,len:hexdata"
    const auto comma = args.find(',');
    const auto colon = args.find(':');
    if (comma == std::string::npos || colon == std::string::npos) return "E01";

    vaddr_t addr = 0;
    size_t  len  = 0;
    try {
        addr = static_cast<vaddr_t>(std::stoull(args.substr(0, comma), nullptr, 16));
        len  = static_cast<size_t> (std::stoull(args.substr(comma + 1, colon - comma - 1), nullptr, 16));
    } catch (...) {
        return "E01";
    }

    const std::string hex_data = args.substr(colon + 1);
    auto bytes = HexToBytes(hex_data);
    if (bytes.size() < len) bytes.resize(len, 0);

    if (memory_ != nullptr) {
        memory_->WriteBlock(addr, bytes.data(), len);
    }

    return "OK";
}

std::string GdbStub::HandleQuery(const std::string& query) {
    if (query.rfind("Supported", 0) == 0) {
        return "PacketSize=4000;qXfer:features:read-";
    }
    if (query == "Attached") {
        return "1";
    }
    if (query == "C") {
        return "QC1";
    }
    // Unknown query
    return "";
}

} // namespace nemu::core::debug
