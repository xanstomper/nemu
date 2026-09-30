#pragma once

// ---------------------------------------------------------------------------
// GDB Remote Serial Protocol (RSP) TCP stub for NEMU.
//
// Exposes a minimal GDB RSP server so a host GDB or LLDB instance can attach
// to the emulated AArch64 guest over a plain TCP connection.
//
// Protocol reference: https://sourceware.org/gdb/current/onlinedocs/gdb/Remote-Protocol.html
//
// Supported packets:
//   '?'              halt reason      → T05 (SIGTRAP)
//   'g'              read all regs    → 31×16-hex-digit zero registers (x0–x30)
//   'G…'             write all regs   → OK
//   'p N'            read reg N       → 16 hex digits (8 zero bytes)
//   'P N=value'      write reg N      → OK
//   'm addr,len'     read memory      → hex bytes (from VirtualMemory if set)
//   'M addr,len:data'write memory     → OK
//   'c'              continue         → S05
//   's'              single-step      → S05
//   'H…'             set thread       → OK
//   'qSupported'     feature query    → PacketSize=4000
//   'qAttached'      attached?        → 1
//   'qC'             current thread   → QC1
//   'D'              detach           → OK  (closes client connection)
//   '+' / '-'        ACK / NAK        (consumed silently)
//   everything else                   → empty reply ''
//
// Platform notes:
//   Linux  → POSIX sockets  (sys/socket.h, netinet/in.h, unistd.h)
//   Windows→ Winsock2       (winsock2.h, ws2tcpip.h)
// ---------------------------------------------------------------------------

#include "core/types.hpp"
#include "core/memory/virtual_memory.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using SocketFd = SOCKET;
static constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
#else
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
using SocketFd = int;
static constexpr SocketFd kInvalidSocket = -1;
#endif

namespace nemu::core::debug {

class GdbStub {
public:
    // Construct without an attached memory bus (memory reads return zeros).
    GdbStub() = default;

    // Construct with access to the guest address space for 'm'/'M' packets.
    explicit GdbStub(memory::VirtualMemory* mem) : memory_(mem) {}

    ~GdbStub();

    // Not copyable / movable.
    GdbStub(const GdbStub&)            = delete;
    GdbStub& operator=(const GdbStub&) = delete;

    // Start listening on `port`.  No-op if already running.
    // Returns true on success.
    bool Start(u16 port);

    // Stop the server: close the listen socket and join the background thread.
    // No-op if not running.
    void Stop();

    [[nodiscard]] bool IsRunning() const noexcept { return running_.load(std::memory_order_relaxed); }

    // Optionally wire in a live VirtualMemory after construction.
    void SetMemory(memory::VirtualMemory* mem) noexcept { memory_ = mem; }

private:
    // Background thread entry – accept loop + per-client handler.
    void ServerThread();

    // Handle a single connected client until it disconnects or stop is requested.
    void HandleClient(SocketFd client_fd);

    // -----------------------------------------------------------------------
    // RSP helpers
    // -----------------------------------------------------------------------

    // Read one raw byte from client_fd.  Returns false on error/EOF.
    bool RecvByte(SocketFd fd, char& out);

    // Send raw bytes (handles partial writes).
    bool SendRaw(SocketFd fd, const char* data, int len);
    bool SendRaw(SocketFd fd, const std::string& s);

    // Read a complete $packet#cc frame.  Sends '+' ACK on success, '-' on bad checksum.
    // Returns the payload (between $ and #) or empty string on disconnect.
    std::string RecvPacket(SocketFd fd);

    // Encode payload as $payload#cc and send it (preceded by '+' ACK).
    bool SendPacket(SocketFd fd, const std::string& payload);

    // Dispatch a decoded RSP packet and return the reply payload.
    std::string DispatchPacket(const std::string& pkt);

    // -----------------------------------------------------------------------
    // Packet handlers
    // -----------------------------------------------------------------------
    std::string HandleHaltReason();
    std::string HandleReadRegisters();
    std::string HandleWriteRegisters(const std::string& data);
    std::string HandleReadRegister(const std::string& args);
    std::string HandleWriteRegister(const std::string& args);
    std::string HandleReadMemory(const std::string& args);
    std::string HandleWriteMemory(const std::string& args);
    std::string HandleQuery(const std::string& query);

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    memory::VirtualMemory* memory_{nullptr};

    std::atomic<bool> running_{false};
    std::thread        server_thread_;

    // Listen socket; closed by Stop() to unblock accept().
    SocketFd listen_fd_{kInvalidSocket};
    std::mutex listen_fd_mutex_;   // guard around listen_fd_ for Stop() races

#ifdef _WIN32
    bool wsa_initialized_{false};
#endif
};

} // namespace nemu::core::debug
